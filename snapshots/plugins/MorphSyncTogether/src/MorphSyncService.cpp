#include "PCH.h"
#include "MorphSyncService.h"
#include "AppearanceProbe.h"
#include "UdpTransport.h"

namespace MorphSyncTogether
{
    namespace
    {
        class CaptureVisitor final : public SKEE::IBodyMorphInterface::MorphValueVisitor
        {
        public:
            struct Value
            {
                std::string morphName;
                std::string morphKey;
                float value{ 0.0F };
            };

            void Visit(
                RE::TESObjectREFR*,
                const char* morphName,
                const char* morphKey,
                float value) override
            {
                if (!morphName || !*morphName || !morphKey || !*morphKey || !std::isfinite(value)) {
                    return;
                }

                values.push_back(Value{
                    morphName,
                    morphKey,
                    value });
            }

            std::vector<Value> values;
        };

        std::uint64_t ParseHex64(const std::optional<std::string>& text)
        {
            if (!text || text->empty()) {
                return 0;
            }

            try {
                return std::stoull(*text, nullptr, 16);
            } catch (...) {
                return 0;
            }
        }

        std::optional<std::uint32_t> ParseHex32(
            const std::optional<std::string>& text)
        {
            if (!text || text->empty()) {
                return std::nullopt;
            }
            try {
                const auto value = std::stoull(*text, nullptr, 16);
                if (value > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                return static_cast<std::uint32_t>(value);
            } catch (...) {
                return std::nullopt;
            }
        }

        std::optional<std::size_t> ParseSize(const std::optional<std::string>& text)
        {
            if (!text || text->empty()) {
                return std::nullopt;
            }

            try {
                return static_cast<std::size_t>(std::stoull(*text));
            } catch (...) {
                return std::nullopt;
            }
        }

        bool PubicOverlayEquivalent(
            const AppearanceProbe::PubicOverlayState& lhs,
            const AppearanceProbe::PubicOverlayState& rhs)
        {
            if (lhs.present != rhs.present) {
                return false;
            }
            if (!lhs.present) {
                return true;
            }

            const auto equalPath = lhs.texturePath.size() == rhs.texturePath.size() &&
                std::equal(
                    lhs.texturePath.begin(), lhs.texturePath.end(), rhs.texturePath.begin(),
                    [](char a, char b) {
                        const auto normalize = [](char value) {
                            return value == '/' ? '\\' : static_cast<char>(
                                std::tolower(static_cast<unsigned char>(value)));
                        };
                        return normalize(a) == normalize(b);
                    });
            return equalPath && lhs.color == rhs.color &&
                   std::abs(lhs.alpha - rhs.alpha) <= 0.01F;
        }
    }

    MorphSyncService& MorphSyncService::GetSingleton()
    {
        static MorphSyncService singleton;
        return singleton;
    }

    MorphSyncService::~MorphSyncService()
    {
        Stop();
    }

    void MorphSyncService::Initialize()
    {
        if (_initialized) {
            return;
        }
        _initialized = true;

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            SKSE::log::critical("MST RaceMenu init failed: no SKSE messaging interface");
            return;
        }

        SKEE::InterfaceExchangeMessage exchange{};
        const bool dispatched = messaging->Dispatch(
            SKEE::InterfaceExchangeMessage::kMessageExchangeInterface,
            &exchange,
            sizeof(exchange),
            "skee");

        if (!dispatched || !exchange.interfaceMap) {
            SKSE::log::critical(
                "MST RaceMenu interface exchange failed dispatched={} map={}",
                dispatched ? 1 : 0,
                exchange.interfaceMap ? 1 : 0);
            return;
        }

        auto* base = exchange.interfaceMap->QueryInterface("BodyMorph");
        if (!base) {
            SKSE::log::critical("MST RaceMenu BodyMorph interface not found");
            return;
        }

        _bodyMorph = static_cast<SKEE::IBodyMorphInterface*>(base);
        SKSE::log::info(
            "MST RaceMenu BodyMorph interface READY version={}",
            _bodyMorph->GetVersion());

        AppearanceProbe::GetSingleton().Initialize(exchange.interfaceMap);
    }

    void MorphSyncService::Start()
    {
        if (_running.load()) {
            return;
        }

        _config = Config::Load();
        if (!_config.networkEnabled) {
            SKSE::log::warn("MorphSyncTogether disabled by INI");
            return;
        }

        if (!_bodyMorph) {
            SKSE::log::critical("MorphSyncTogether cannot start: RaceMenu BodyMorph unavailable");
            return;
        }

        AppearanceProbe::GetSingleton().Configure(
            _config.appearanceProbeEnabled,
            _config.appearanceProbeIntervalMs,
            _config.appearanceProbeVerbose,
            _config.appearancePreserveRemote,
            _config.appearanceRecoveryAttempts,
            _config.faceMaterialRebindEnabled,
            _config.faceMaterialRebindFollowups,
            _config.faceMaterialRebindIntervalMs);

        _running.store(true);
        _syncThread = std::jthread([this](std::stop_token token) {
            SyncLoop(token);
        });

        SKSE::log::info(
            "Morph sync started interval={}ms resend={}ms remoteReapply={}ms clearRemote={} pubicOverlaySync={}",
            _config.syncIntervalMs,
            _config.fullResendMs,
            _config.remoteReapplyMs,
            _config.clearRemoteMorphs ? 1 : 0,
            _config.pubicOverlaySyncEnabled ? 1 : 0);

        QueueTick();
    }

    void MorphSyncService::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_syncThread.joinable()) {
            _syncThread.request_stop();
            _syncThread.join();
        }

        _tickQueued.store(false);
        SKSE::log::info("Morph sync stopped");
    }

    void MorphSyncService::Reset()
    {
        _lastSentHash = 0;
        _lastSentName.clear();
        _lastSentAt = {};
        _lastSentPubicHash = 0;
        _lastSentPubicName.clear();
        _lastSentPubicAt = {};

        {
            std::scoped_lock lock(_remoteMutex);
            _assemblies.clear();
            _remoteSnapshots.clear();
            _remotePubicSnapshots.clear();
        }

        AppearanceProbe::GetSingleton().Reset();
        SKSE::log::info("Morph sync state reset");
    }

    void MorphSyncService::SyncLoop(std::stop_token stopToken)
    {
        while (!stopToken.stop_requested() && _running.load()) {
            QueueTick();

            const auto total = std::chrono::milliseconds(_config.syncIntervalMs);
            constexpr auto slice = std::chrono::milliseconds(100);
            auto slept = std::chrono::milliseconds(0);

            while (slept < total && !stopToken.stop_requested() && _running.load()) {
                std::this_thread::sleep_for(slice);
                slept += slice;
            }
        }
    }

    void MorphSyncService::QueueTick()
    {
        if (!_running.load() || _tickQueued.exchange(true)) {
            return;
        }

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            _tickQueued.store(false);
            return;
        }

        tasks->AddTask([this]() {
            _tickQueued.store(false);
            TickOnGameThread();
        });
    }

    void MorphSyncService::TickOnGameThread()
    {
        if (!_running.load() || !_bodyMorph) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        const char* rawName = player->GetName();
        const std::string playerName = rawName ? rawName : "";
        if (playerName.empty()) {
            return;
        }

        AppearanceProbe::GetSingleton().ProbeLocalPlayer(player);

        const auto values = CaptureMorphs(player);
        const auto hash = HashMorphs(values);
        const auto now = std::chrono::steady_clock::now();

        const bool nameChanged = playerName != _lastSentName;
        const bool hashChanged = hash != _lastSentHash;
        const bool resendDue =
            _lastSentAt.time_since_epoch().count() == 0 ||
            now - _lastSentAt >= std::chrono::milliseconds(_config.fullResendMs);

        if (nameChanged || hashChanged || resendDue) {
            BroadcastSnapshot(player, values, hash);
            _lastSentName = playerName;
            _lastSentHash = hash;
            _lastSentAt = now;
        }

        if (_config.pubicOverlaySyncEnabled) {
            const auto pubic = AppearanceProbe::GetSingleton().CapturePubicOverlay(player);
            if (pubic) {
                const auto pubicHash = HashPubicOverlay(*pubic);
                const bool pubicNameChanged = playerName != _lastSentPubicName;
                const bool pubicHashChanged = pubicHash != _lastSentPubicHash;
                const bool pubicResendDue =
                    _lastSentPubicAt.time_since_epoch().count() == 0 ||
                    now - _lastSentPubicAt >= std::chrono::milliseconds(_config.fullResendMs);
                if (pubicNameChanged || pubicHashChanged || pubicResendDue) {
                    BroadcastPubicOverlay(player, *pubic, pubicHash);
                    _lastSentPubicName = playerName;
                    _lastSentPubicHash = pubicHash;
                    _lastSentPubicAt = now;
                }
            }
        }

        std::unordered_set<std::string> senderSet;
        {
            std::scoped_lock lock(_remoteMutex);
            for (const auto& [sender, snapshot] : _remoteSnapshots) {
                senderSet.insert(sender);
            }
            for (const auto& [sender, snapshot] : _remotePubicSnapshots) {
                senderSet.insert(sender);
            }
        }

        for (const auto& sender : senderSet) {
            if (auto* proxy = ResolveRemoteProxyByName(sender)) {
                AppearanceProbe::GetSingleton().ProbeRemoteProxy(sender, proxy);
            }
            TryApplyRemote(sender, false);
            TryApplyRemotePubic(sender, false);
        }

        // Drop incomplete chunk assemblies after 15 seconds.
        {
            std::scoped_lock lock(_remoteMutex);
            for (auto it = _assemblies.begin(); it != _assemblies.end();) {
                if (now - it->second.updated > std::chrono::seconds(15)) {
                    it = _assemblies.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    std::vector<MorphSyncService::MorphValue>
    MorphSyncService::CaptureMorphs(RE::TESObjectREFR* actor) const
    {
        CaptureVisitor visitor;
        if (!actor || !_bodyMorph) {
            return {};
        }
        _bodyMorph->VisitMorphValues(actor, visitor);

        std::vector<MorphValue> result;
        result.reserve(visitor.values.size());
        for (auto& value : visitor.values) {
            result.push_back(MorphValue{
                std::move(value.morphName),
                std::move(value.morphKey),
                value.value });
        }

        std::sort(result.begin(), result.end(), [](const MorphValue& a, const MorphValue& b) {
            if (a.morphName != b.morphName) {
                return a.morphName < b.morphName;
            }
            if (a.morphKey != b.morphKey) {
                return a.morphKey < b.morphKey;
            }
            return a.value < b.value;
        });

        return result;
    }

    std::uint64_t MorphSyncService::HashMorphs(const std::vector<MorphValue>& values) const
    {
        std::uint64_t hash = 1469598103934665603ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;

        auto feedByte = [&](std::uint8_t byte) {
            hash ^= byte;
            hash *= prime;
        };

        auto feedText = [&](std::string_view text) {
            for (const unsigned char ch : text) {
                feedByte(ch);
            }
            feedByte(0xFF);
        };

        for (const auto& value : values) {
            feedText(value.morphName);
            feedText(value.morphKey);
            const auto bits = std::bit_cast<std::uint32_t>(value.value);
            feedByte(static_cast<std::uint8_t>((bits >> 0) & 0xFF));
            feedByte(static_cast<std::uint8_t>((bits >> 8) & 0xFF));
            feedByte(static_cast<std::uint8_t>((bits >> 16) & 0xFF));
            feedByte(static_cast<std::uint8_t>((bits >> 24) & 0xFF));
        }

        return hash;
    }

    void MorphSyncService::BroadcastSnapshot(
        RE::PlayerCharacter* player,
        const std::vector<MorphValue>& values,
        std::uint64_t hash)
    {
        if (!UdpTransport::GetSingleton().IsRunning()) {
            return;
        }

        std::vector<std::string> chunks;
        std::string current;
        constexpr std::size_t kChunkLimit = 2200;

        for (const auto& value : values) {
            const auto token = fmt::format(
                "{},{},{:.9g}",
                HexEncode(value.morphName),
                HexEncode(value.morphKey),
                value.value);

            const std::size_t extra = token.size() + (current.empty() ? 0 : 1);
            if (!current.empty() && current.size() + extra > kChunkLimit) {
                chunks.push_back(std::move(current));
                current.clear();
            }

            if (!current.empty()) {
                current += ';';
            }
            current += token;
        }

        if (!current.empty()) {
            chunks.push_back(std::move(current));
        }
        if (chunks.empty()) {
            chunks.emplace_back();
        }

        SKSE::log::info(
            "MST MORPH TX SNAPSHOT player={:08X} name=\"{}\" values={} chunks={} hash={:016X} hasMorphs={}",
            player->GetFormID(),
            player->GetName(),
            values.size(),
            chunks.size(),
            hash,
            _bodyMorph->HasMorphs(player) ? 1 : 0);

        for (std::size_t i = 0; i < chunks.size(); ++i) {
            UdpTransport::GetSingleton().Send(fmt::format(
                "MORPH|hash={:016X}|seq={}|count={}|props={}",
                hash,
                i,
                chunks.size(),
                chunks[i]));
        }
    }

    std::uint64_t MorphSyncService::HashPubicOverlay(
        const AppearanceProbe::PubicOverlayState& state) const
    {
        std::uint64_t hash = 1469598103934665603ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;
        auto feedByte = [&](std::uint8_t value) {
            hash ^= value;
            hash *= prime;
        };

        feedByte(state.present ? 1 : 0);
        feedByte(state.female ? 1 : 0);
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            feedByte(static_cast<std::uint8_t>((state.sourceSlot >> shift) & 0xFF));
        }
        for (const unsigned char ch : state.texturePath) {
            feedByte(ch);
        }
        feedByte(0xFF);
        const auto color = static_cast<std::uint32_t>(state.color);
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            feedByte(static_cast<std::uint8_t>((color >> shift) & 0xFF));
        }
        const auto alpha = std::bit_cast<std::uint32_t>(state.alpha);
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            feedByte(static_cast<std::uint8_t>((alpha >> shift) & 0xFF));
        }
        return hash == 0 ? 1 : hash;
    }

    void MorphSyncService::BroadcastPubicOverlay(
        RE::PlayerCharacter* player,
        const AppearanceProbe::PubicOverlayState& state,
        std::uint64_t hash)
    {
        if (!player || !UdpTransport::GetSingleton().IsRunning()) {
            return;
        }

        SKSE::log::info(
            "MST PUBES TX player={:08X} name=\"{}\" present={} female={} slot={} texture=\"{}\" color={:08X} alpha={:.4f} hash={:016X}",
            player->GetFormID(),
            player->GetName(),
            state.present ? 1 : 0,
            state.female ? 1 : 0,
            state.sourceSlot,
            state.texturePath,
            static_cast<std::uint32_t>(state.color),
            state.alpha,
            hash);

        UdpTransport::GetSingleton().Send(fmt::format(
            "PUBES|hash={:016X}|present={}|female={}|slot={}|texture={}|color={:08X}|alpha={:.9g}",
            hash,
            state.present ? 1 : 0,
            state.female ? 1 : 0,
            state.sourceSlot,
            HexEncode(state.texturePath),
            static_cast<std::uint32_t>(state.color),
            state.alpha));
    }

    void MorphSyncService::HandleUdpPacket(std::string packet)
    {
        constexpr std::string_view prefix = "MSTUDP|v1|";
        if (!packet.starts_with(prefix)) {
            return;
        }

        const auto sender = ReadField(packet, "from");
        if (!sender || sender->empty()) {
            SKSE::log::warn("MST RX packet missing sender");
            return;
        }

        const auto pubicPos = packet.find("|PUBES|");
        if (pubicPos != std::string::npos) {
            HandlePubicPacket(*sender, std::string_view(packet).substr(pubicPos + 1));
            return;
        }

        const auto morphPos = packet.find("|MORPH|");
        if (morphPos != std::string::npos) {
            HandleMorphPacket(*sender, std::string_view(packet).substr(morphPos + 1));
        }
    }

    void MorphSyncService::HandleMorphPacket(
        std::string_view senderView,
        std::string_view payload)
    {
        const std::string sender(senderView);
        const auto hash = ParseHex64(ReadField(payload, "hash"));
        const auto sequence = ParseSize(ReadField(payload, "seq"));
        const auto count = ParseSize(ReadField(payload, "count"));
        const auto props = ReadField(payload, "props");

        if (hash == 0 || !sequence || !count || !props || *count == 0 || *count > 128 || *sequence >= *count) {
            SKSE::log::warn("MST MORPH RX invalid packet sender=\"{}\"", sender);
            return;
        }

        bool complete = false;
        {
            std::scoped_lock lock(_remoteMutex);
            auto& assembly = _assemblies[sender];
            if (assembly.hash != hash || assembly.expectedChunks != *count) {
                assembly = {};
                assembly.hash = hash;
                assembly.expectedChunks = *count;
                assembly.chunks.resize(*count);
                assembly.received.assign(*count, false);
            }

            assembly.chunks[*sequence] = *props;
            assembly.received[*sequence] = true;
            assembly.updated = std::chrono::steady_clock::now();
            complete = std::all_of(
                assembly.received.begin(),
                assembly.received.end(),
                [](bool value) { return value; });
        }

        SKSE::log::info(
            "MST MORPH RX CHUNK sender=\"{}\" hash={:016X} seq={}/{} bytes={} complete={}",
            sender,
            hash,
            *sequence + 1,
            *count,
            props->size(),
            complete ? 1 : 0);

        if (!complete) {
            return;
        }

        std::vector<MorphValue> values;
        std::uint32_t invalid = 0;
        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _assemblies.find(sender);
            if (it == _assemblies.end()) {
                return;
            }

            for (const auto& chunk : it->second.chunks) {
                if (chunk.empty()) {
                    continue;
                }

                for (const auto& token : Split(chunk, ';')) {
                    if (token.empty()) {
                        continue;
                    }

                    const auto fields = Split(token, ',');
                    if (fields.size() != 3) {
                        ++invalid;
                        continue;
                    }

                    try {
                        const auto morphName = HexDecode(fields[0]);
                        const auto morphKey = HexDecode(fields[1]);
                        const float value = std::stof(fields[2]);
                        if (!morphName || !morphKey || morphName->empty() || morphKey->empty() || !std::isfinite(value)) {
                            ++invalid;
                            continue;
                        }

                        values.push_back(MorphValue{ *morphName, *morphKey, value });
                    } catch (...) {
                        ++invalid;
                    }
                }
            }

            _assemblies.erase(it);

            auto& snapshot = _remoteSnapshots[sender];
            snapshot.hash = hash;
            snapshot.values = values;
            snapshot.everApplied = false;
            snapshot.lastActorFormID = 0;
            snapshot.lastApply = {};
        }

        SKSE::log::info(
            "MST MORPH RX COMPLETE sender=\"{}\" hash={:016X} values={} invalid={}",
            sender,
            hash,
            values.size(),
            invalid);

        TryApplyRemote(sender, true);
    }

    void MorphSyncService::HandlePubicPacket(
        std::string_view senderView,
        std::string_view payload)
    {
        if (!_config.pubicOverlaySyncEnabled) {
            return;
        }

        const std::string sender(senderView);
        const auto hash = ParseHex64(ReadField(payload, "hash"));
        const auto present = ParseSize(ReadField(payload, "present"));
        const auto female = ParseSize(ReadField(payload, "female"));
        const auto slot = ParseSize(ReadField(payload, "slot"));
        const auto textureField = ReadField(payload, "texture");
        const auto color = ParseHex32(ReadField(payload, "color"));
        const auto alphaField = ReadField(payload, "alpha");

        if (hash == 0 || !present || *present > 1 || !female || *female > 1 ||
            !slot || *slot >= 64 || !textureField || !color || !alphaField) {
            SKSE::log::warn("MST PUBES RX invalid packet sender=\"{}\"", sender);
            return;
        }

        const auto texture = HexDecode(*textureField);
        float alpha = 0.0F;
        try {
            alpha = std::stof(*alphaField);
        } catch (...) {
            SKSE::log::warn("MST PUBES RX invalid alpha sender=\"{}\"", sender);
            return;
        }
        if (!texture || texture->size() > 512 || !std::isfinite(alpha) ||
            (*present != 0 && texture->empty())) {
            SKSE::log::warn("MST PUBES RX invalid values sender=\"{}\"", sender);
            return;
        }

        AppearanceProbe::PubicOverlayState state{};
        state.present = *present != 0;
        state.female = *female != 0;
        state.sourceSlot = static_cast<std::uint32_t>(*slot);
        state.texturePath = *texture;
        state.color = static_cast<std::int32_t>(*color);
        state.alpha = std::clamp(alpha, 0.0F, 1.0F);

        const auto dataHash = HashPubicOverlay(state);
        if (dataHash != hash) {
            SKSE::log::warn(
                "MST PUBES RX hash mismatch sender=\"{}\" net={:016X} data={:016X}",
                sender,
                hash,
                dataHash);
            return;
        }

        {
            std::scoped_lock lock(_remoteMutex);
            auto& snapshot = _remotePubicSnapshots[sender];
            snapshot.hash = hash;
            snapshot.state = state;
            snapshot.lastActorFormID = 0;
            snapshot.lastApply = {};
            snapshot.everApplied = false;
        }

        SKSE::log::info(
            "MST PUBES RX sender=\"{}\" present={} female={} slot={} texture=\"{}\" color={:08X} alpha={:.4f} hash={:016X}",
            sender,
            state.present ? 1 : 0,
            state.female ? 1 : 0,
            state.sourceSlot,
            state.texturePath,
            static_cast<std::uint32_t>(state.color),
            state.alpha,
            hash);

        TryApplyRemotePubic(sender, true);
    }

    void MorphSyncService::TryApplyRemote(
        const std::string& sender,
        bool force)
    {
        RemoteSnapshot snapshot;
        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remoteSnapshots.find(sender);
            if (it == _remoteSnapshots.end()) {
                return;
            }
            snapshot = it->second;
        }

        auto* actor = ResolveRemoteProxyByName(sender);
        if (!actor) {
            if (force) {
                SKSE::log::info(
                    "MST MORPH APPLY WAIT sender=\"{}\" reason=proxy-not-found",
                    sender);
            }
            return;
        }

        if (!actor->Get3D()) {
            if (force) {
                SKSE::log::info(
                    "MST MORPH APPLY WAIT sender=\"{}\" actor={:08X} reason=proxy-3d-not-loaded",
                    sender,
                    actor->GetFormID());
            }
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto authoritativeDataHash = HashMorphs(snapshot.values);
        const auto liveBefore = CaptureMorphs(actor);
        const auto liveBeforeHash = HashMorphs(liveBefore);
        const bool drift = liveBeforeHash != authoritativeDataHash;

        if (!force && snapshot.everApplied && snapshot.lastActorFormID == actor->GetFormID() &&
            !drift &&
            now - snapshot.lastApply < std::chrono::milliseconds(_config.remoteReapplyMs)) {
            return;
        }

        if (drift) {
            SKSE::log::info(
                "MST MORPH DRIFT sender=\"{}\" actor={:08X} liveValues={} liveHash={:016X} authoritativeValues={} authoritativeHash={:016X} action=restore",
                sender,
                actor->GetFormID(),
                liveBefore.size(),
                liveBeforeHash,
                snapshot.values.size(),
                authoritativeDataHash);
        }

        if (_config.clearRemoteMorphs) {
            _bodyMorph->ClearMorphs(actor);
        }

        for (const auto& value : snapshot.values) {
            _bodyMorph->SetMorph(
                actor,
                value.morphName.c_str(),
                value.morphKey.c_str(),
                value.value);
        }

        // Apply immediately on the game thread, then queue RaceMenu's own model-weight
        // update as a second pass. The second pass is important when OBody/RaceMenu has
        // just queued its own body update for the same proxy.
        _bodyMorph->ApplyBodyMorphs(actor, false);
        _bodyMorph->UpdateModelWeight(actor, false);

        // Verify the authoritative RaceMenu data itself. This is intentionally not a
        // visual/GPU verification; it tells us whether another morph owner already
        // overwrote the proxy again during this game-thread pass.
        const auto liveAfter = CaptureMorphs(actor);
        const auto liveAfterHash = HashMorphs(liveAfter);
        const bool verified = liveAfterHash == authoritativeDataHash;

        {
            std::scoped_lock lock(_remoteMutex);
            auto it = _remoteSnapshots.find(sender);
            if (it != _remoteSnapshots.end() && it->second.hash == snapshot.hash) {
                it->second.everApplied = true;
                it->second.lastActorFormID = actor->GetFormID();
                it->second.lastApply = now;
            }
        }

        auto* base = actor->GetActorBase();
        SKSE::log::info(
            "MST MORPH APPLY sender=\"{}\" actor={:08X} base={:08X} values={} netHash={:016X} dataHash={:016X} clear={} verified={} liveAfterValues={} liveAfterHash={:016X} modelUpdateQueued=1",
            sender,
            actor->GetFormID(),
            base ? base->GetFormID() : 0,
            snapshot.values.size(),
            snapshot.hash,
            authoritativeDataHash,
            _config.clearRemoteMorphs ? 1 : 0,
            verified ? 1 : 0,
            liveAfter.size(),
            liveAfterHash);
    }

    void MorphSyncService::TryApplyRemotePubic(
        const std::string& sender,
        bool force)
    {
        if (!_config.pubicOverlaySyncEnabled) {
            return;
        }

        RemotePubicSnapshot snapshot;
        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remotePubicSnapshots.find(sender);
            if (it == _remotePubicSnapshots.end()) {
                return;
            }
            snapshot = it->second;
        }

        auto* actor = ResolveRemoteProxyByName(sender);
        if (!actor) {
            if (force) {
                SKSE::log::info(
                    "MST PUBES APPLY WAIT sender=\"{}\" reason=proxy-not-found",
                    sender);
            }
            return;
        }
        if (!actor->Get3D()) {
            if (force) {
                SKSE::log::info(
                    "MST PUBES APPLY WAIT sender=\"{}\" actor={:08X} reason=proxy-3d-not-loaded",
                    sender,
                    actor->GetFormID());
            }
            return;
        }

        auto& appearance = AppearanceProbe::GetSingleton();
        const auto liveBefore = appearance.CapturePubicOverlay(actor);
        if (!liveBefore) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool drift = !PubicOverlayEquivalent(*liveBefore, snapshot.state);
        if (!force && snapshot.everApplied && snapshot.lastActorFormID == actor->GetFormID() &&
            !drift &&
            now - snapshot.lastApply < std::chrono::milliseconds(_config.remoteReapplyMs)) {
            return;
        }

        if (drift) {
            SKSE::log::info(
                "MST PUBES DRIFT sender=\"{}\" actor={:08X} livePresent={} liveSlot={} liveTexture=\"{}\" authoritativePresent={} authoritativeSlot={} authoritativeTexture=\"{}\" action=restore",
                sender,
                actor->GetFormID(),
                liveBefore->present ? 1 : 0,
                liveBefore->sourceSlot,
                liveBefore->texturePath,
                snapshot.state.present ? 1 : 0,
                snapshot.state.sourceSlot,
                snapshot.state.texturePath);
        }

        const bool applied = appearance.ApplyPubicOverlay(actor, snapshot.state);
        const auto liveAfter = appearance.CapturePubicOverlay(actor);
        const bool verified = liveAfter &&
            PubicOverlayEquivalent(*liveAfter, snapshot.state);

        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remotePubicSnapshots.find(sender);
            if (it != _remotePubicSnapshots.end() && it->second.hash == snapshot.hash) {
                it->second.everApplied = true;
                it->second.lastActorFormID = actor->GetFormID();
                it->second.lastApply = now;
            }
        }

        SKSE::log::info(
            "MST PUBES APPLY sender=\"{}\" actor={:08X} applied={} verified={} present={} targetSlot={} texture=\"{}\" color={:08X} alpha={:.4f}",
            sender,
            actor->GetFormID(),
            applied ? 1 : 0,
            verified ? 1 : 0,
            snapshot.state.present ? 1 : 0,
            liveAfter ? liveAfter->sourceSlot : 0,
            liveAfter ? liveAfter->texturePath : std::string{},
            liveAfter ? static_cast<std::uint32_t>(liveAfter->color) : 0,
            liveAfter ? liveAfter->alpha : 0.0F);
    }

    RE::Actor* MorphSyncService::ResolveRemoteProxyByName(std::string_view name) const
    {
        if (name.empty()) {
            return nullptr;
        }

        auto* processLists = RE::ProcessLists::GetSingleton();
        auto* localPlayer = RE::PlayerCharacter::GetSingleton();
        if (!processLists) {
            return nullptr;
        }

        struct Candidate
        {
            RE::Actor* actor{ nullptr };
            float distance{ 0.0F };
        };

        std::vector<Candidate> candidates;
        std::unordered_set<RE::FormID> seen;

        auto considerActor = [&](RE::Actor* actor) {
            if (!actor || actor == localPlayer || !IsLikelySTRProxy(actor)) {
                return;
            }

            if (!seen.insert(actor->GetFormID()).second) {
                return;
            }

            const char* rawName = actor->GetName();
            if (!rawName || !EqualsInsensitive(rawName, name)) {
                return;
            }

            float distance = 0.0F;
            if (localPlayer) {
                distance = actor->GetPosition().GetDistance(localPlayer->GetPosition());
            }

            candidates.push_back(Candidate{ actor, distance });
        };

        auto scanHandles = [&](const auto& handles) {
            for (const auto& handle : handles) {
                const auto ptr = handle.get();
                considerActor(ptr.get());
            }
        };

        scanHandles(processLists->highActorHandles);
        scanHandles(processLists->middleHighActorHandles);
        scanHandles(processLists->middleLowActorHandles);
        scanHandles(processLists->lowActorHandles);

        if (candidates.empty()) {
            return nullptr;
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.distance < b.distance;
        });

        return candidates.front().actor;
    }

    bool MorphSyncService::IsLikelySTRProxy(RE::Actor* actor) const
    {
        if (!actor || actor->IsPlayerRef()) {
            return false;
        }

        auto* base = actor->GetActorBase();
        if (!base) {
            return false;
        }

        constexpr RE::FormID dynamicMask = 0xFF000000;
        return (actor->GetFormID() & dynamicMask) == dynamicMask &&
               (base->GetFormID() & dynamicMask) == dynamicMask;
    }

    bool MorphSyncService::EqualsInsensitive(std::string_view a, std::string_view b)
    {
        return a.size() == b.size() && std::equal(
            a.begin(), a.end(), b.begin(), b.end(),
            [](char lhs, char rhs) {
                return std::tolower(static_cast<unsigned char>(lhs)) ==
                       std::tolower(static_cast<unsigned char>(rhs));
            });
    }

    std::string MorphSyncService::HexEncode(std::string_view value)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(value.size() * 2);
        for (const unsigned char ch : value) {
            out.push_back(hex[(ch >> 4) & 0x0F]);
            out.push_back(hex[ch & 0x0F]);
        }
        return out;
    }

    std::optional<std::string> MorphSyncService::HexDecode(std::string_view value)
    {
        if ((value.size() % 2) != 0) {
            return std::nullopt;
        }

        auto nibble = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
            if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
            return -1;
        };

        std::string out;
        out.reserve(value.size() / 2);
        for (std::size_t i = 0; i < value.size(); i += 2) {
            const int hi = nibble(value[i]);
            const int lo = nibble(value[i + 1]);
            if (hi < 0 || lo < 0) {
                return std::nullopt;
            }
            out.push_back(static_cast<char>((hi << 4) | lo));
        }
        return out;
    }

    std::vector<std::string> MorphSyncService::Split(std::string_view text, char delimiter)
    {
        std::vector<std::string> result;
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto pos = text.find(delimiter, start);
            if (pos == std::string_view::npos) {
                result.emplace_back(text.substr(start));
                break;
            }
            result.emplace_back(text.substr(start, pos - start));
            start = pos + 1;
        }
        return result;
    }

    std::optional<std::string> MorphSyncService::ReadField(
        std::string_view packet,
        std::string_view key)
    {
        const auto needle = fmt::format("{}=", key);
        auto pos = packet.find(needle);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }

        pos += needle.size();
        const auto end = packet.find('|', pos);
        if (end == std::string_view::npos) {
            return std::string(packet.substr(pos));
        }
        return std::string(packet.substr(pos, end - pos));
    }
}
