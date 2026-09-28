#include "PCH.h"
#include "AddonBridge.h"

#include "ActorResolver.h"
#include "OCumOverlayVisibility.h"
#include "PapyrusAnimationBridge.h"
#include "RaceMenuOverlayBridge.h"
#include "UdpTransport.h"

namespace OStimTogether
{
    namespace
    {
        constexpr std::string_view kAddonEventName = "ostimtogether_addon";
        constexpr std::string_view kOCumChannel = "ocum";
        constexpr std::string_view kOCumTextureMarker = "CumOverlays";
        constexpr std::string_view kDefaultOverlayTexture =
            "actors\\character\\overlays\\default.dds";

        bool EqualsInsensitive(std::string_view lhs, std::string_view rhs)
        {
            return lhs.size() == rhs.size() &&
                   std::equal(
                       lhs.begin(), lhs.end(), rhs.begin(),
                       [](char a, char b) {
                           return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b));
                       });
        }

        bool IsSafeAddonToken(std::string_view value)
        {
            if (value.empty() || value.size() > 128) {
                return false;
            }

            return std::all_of(
                value.begin(), value.end(),
                [](char ch) {
                    const auto u = static_cast<unsigned char>(ch);
                    return std::isalnum(u) || ch == '_' || ch == '-' || ch == '.';
                });
        }

        std::vector<std::string> SplitText(std::string_view text, char delimiter)
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

        std::string HexEncodeText(std::string_view value)
        {
            static constexpr char kHex[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(value.size() * 2);
            for (const auto ch : value) {
                const auto u = static_cast<unsigned char>(ch);
                out.push_back(kHex[(u >> 4) & 0x0F]);
                out.push_back(kHex[u & 0x0F]);
            }
            return out;
        }

        std::unordered_set<std::string> CollectOverlayNodeKeys(
            const std::vector<std::string>& chunks)
        {
            std::unordered_set<std::string> result;
            for (const auto& chunk : chunks) {
                if (chunk.empty()) {
                    continue;
                }
                for (const auto& raw : SplitText(chunk, ';')) {
                    const auto fields = SplitText(raw, ',');
                    if (fields.size() != 6 || fields[0].empty() || fields[1].empty()) {
                        continue;
                    }
                    result.insert(fmt::format("{}|{}", fields[0], fields[1]));
                }
            }
            return result;
        }

        std::vector<std::string> BuildStaleOverlayClearChunks(
            const std::vector<std::string>& currentChunks,
            const std::vector<std::string>& authoritativeChunks)
        {
            const auto currentNodes = CollectOverlayNodeKeys(currentChunks);
            const auto authoritativeNodes = CollectOverlayNodeKeys(authoritativeChunks);

            std::vector<std::string> tokens;
            const auto defaultTexture = HexEncodeText(kDefaultOverlayTexture);

            for (const auto& key : currentNodes) {
                if (authoritativeNodes.contains(key)) {
                    continue;
                }

                const auto sep = key.find('|');
                if (sep == std::string::npos) {
                    continue;
                }

                const auto female = key.substr(0, sep);
                const auto nodeHex = key.substr(sep + 1);
                if (female.empty() || nodeHex.empty()) {
                    continue;
                }

                tokens.push_back(fmt::format("{},{},0,255,I,0", female, nodeHex));
                tokens.push_back(fmt::format("{},{},2,255,F,0", female, nodeHex));
                tokens.push_back(fmt::format("{},{},3,255,F,0", female, nodeHex));
                tokens.push_back(fmt::format("{},{},7,255,I,0", female, nodeHex));
                tokens.push_back(fmt::format("{},{},8,255,F,0", female, nodeHex));
                tokens.push_back(fmt::format(
                    "{},{},9,0,S,{}",
                    female,
                    nodeHex,
                    defaultTexture));
            }

            std::sort(tokens.begin(), tokens.end());

            std::vector<std::string> chunks;
            std::string current;
            constexpr std::size_t kChunkLimit = 1800;
            for (const auto& token : tokens) {
                const auto extra = token.size() + (current.empty() ? 0 : 1);
                if (!current.empty() && current.size() + extra > kChunkLimit) {
                    chunks.push_back(std::move(current));
                    current.clear();
                }
                if (!current.empty()) {
                    current.push_back(';');
                }
                current += token;
            }
            if (!current.empty()) {
                chunks.push_back(std::move(current));
            }
            return chunks;
        }
    }

    AddonBridge& AddonBridge::GetSingleton()
    {
        static AddonBridge singleton;
        return singleton;
    }

    void AddonBridge::Register()
    {
        if (_registered.exchange(true)) {
            return;
        }

        auto* source = SKSE::GetModCallbackEventSource();
        if (!source) {
            _registered.store(false);
            SKSE::log::warn(
                "OSTNET ADDON bridge: ModCallbackEvent source unavailable");
            return;
        }

        source->AddEventSink(this);
        SKSE::log::info("OSTNET ADDON bridge READY event={}", kAddonEventName);
    }

    std::vector<std::string> AddonBridge::Split(
        std::string_view text,
        char delimiter)
    {
        return SplitText(text, delimiter);
    }

    std::optional<std::string> AddonBridge::Field(
        std::string_view payload,
        std::string_view key)
    {
        const std::string needle = fmt::format("{}=", key);
        std::size_t searchFrom = 0;

        while (searchFrom < payload.size()) {
            const auto begin = payload.find(needle, searchFrom);
            if (begin == std::string_view::npos) {
                return std::nullopt;
            }

            if (begin == 0 || payload[begin - 1] == '|') {
                const auto valueBegin = begin + needle.size();
                const auto end = payload.find('|', valueBegin);
                return end == std::string_view::npos ?
                    std::optional<std::string>{ std::string(payload.substr(valueBegin)) } :
                    std::optional<std::string>{ std::string(payload.substr(valueBegin, end - valueBegin)) };
            }

            searchFrom = begin + needle.size();
        }

        return std::nullopt;
    }

    std::string AddonBridge::HexEncode(std::string_view value)
    {
        return HexEncodeText(value);
    }

    std::optional<std::string> AddonBridge::HexDecode(std::string_view value)
    {
        if ((value.size() % 2) != 0) {
            return std::nullopt;
        }

        const auto nibble = [](char ch) -> int {
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

    RE::BSEventNotifyControl AddonBridge::ProcessEvent(
        const SKSE::ModCallbackEvent* event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*)
    {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const char* rawEventName = event->eventName.c_str();
        if (!rawEventName || !EqualsInsensitive(rawEventName, kAddonEventName)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* actor = event->sender ? event->sender->As<RE::Actor>() : nullptr;
        if (!actor || !actor->IsPlayerRef()) {
            SKSE::log::info(
                "OSTNET ADDON local event ignored sender={:08X} reason=not-local-player",
                actor ? actor->GetFormID() : 0);
            return RE::BSEventNotifyControl::kContinue;
        }

        const char* rawArg = event->strArg.c_str();
        const std::string_view arg = rawArg ? std::string_view(rawArg) : std::string_view{};
        const auto parts = Split(arg, '|');
        if (parts.size() != 3) {
            SKSE::log::warn("OSTNET ADDON local event invalid arg=\"{}\"", arg);
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto& command = parts[0];
        const auto& channel = parts[1];
        const auto& value = parts[2];

        if (!IsSafeAddonToken(channel)) {
            SKSE::log::warn("OSTNET ADDON local event invalid channel=\"{}\"", channel);
            return RE::BSEventNotifyControl::kContinue;
        }

        if (EqualsInsensitive(command, "OVR")) {
            if (value.empty() || value.size() > 512) {
                SKSE::log::warn("OSTNET ADDON OVR invalid marker channel={}", channel);
                return RE::BSEventNotifyControl::kContinue;
            }
            SendOverlayState(actor, channel, value);
            return RE::BSEventNotifyControl::kContinue;
        }

        if (EqualsInsensitive(command, "OBJ")) {
            if (!IsSafeAddonToken(value)) {
                SKSE::log::warn(
                    "OSTNET ADDON OBJ invalid type channel={} type=\"{}\"",
                    channel,
                    value);
                return RE::BSEventNotifyControl::kContinue;
            }
            SendOStimObjectState(actor, channel, value, event->numArg > 0.5F);
            return RE::BSEventNotifyControl::kContinue;
        }

        SKSE::log::warn(
            "OSTNET ADDON local event unknown command=\"{}\" channel={}",
            command,
            channel);
        return RE::BSEventNotifyControl::kContinue;
    }

    void AddonBridge::SendOverlayState(
        RE::Actor* actor,
        std::string_view channel,
        std::string_view textureMarker)
    {
        if (!actor || !actor->IsPlayerRef()) {
            return;
        }

        const char* rawName = actor->GetName();
        const std::string name = rawName ? rawName : "";
        if (name.empty()) {
            return;
        }

        const auto chunks = RaceMenuOverlayBridge::GetSingleton()
            .CaptureMarkedOverlayChunks(actor, textureMarker, 2200);

        RaceMenuOverlayBridge::GetSingleton()
            .RefreshLocalOverlayGeometry(actor, channel, chunks);

        SKSE::log::info(
            "OSTNET ADDON OVR TX channel={} actor={:08X} name=\"{}\" marker=\"{}\" chunks={}",
            channel,
            actor->GetFormID(),
            name,
            textureMarker,
            chunks.size());

        for (std::size_t i = 0; i < chunks.size(); ++i) {
            UdpTransport::GetSingleton().Send(fmt::format(
                "ADDONOVR|channel={}|name={}|seq={}|count={}|props={}",
                HexEncode(channel),
                HexEncode(name),
                i,
                chunks.size(),
                chunks[i]));
        }
    }

    void AddonBridge::SendOStimObjectState(
        RE::Actor* actor,
        std::string_view channel,
        std::string_view objectType,
        bool equipped)
    {
        if (!actor || !actor->IsPlayerRef()) {
            return;
        }

        const char* rawName = actor->GetName();
        const std::string name = rawName ? rawName : "";
        if (name.empty()) {
            return;
        }

        UdpTransport::GetSingleton().Send(fmt::format(
            "ADDONOBJ|channel={}|name={}|type={}|equipped={}",
            HexEncode(channel),
            HexEncode(name),
            HexEncode(objectType),
            equipped ? 1 : 0));

        SKSE::log::info(
            "OSTNET ADDON OBJ TX channel={} actor={:08X} name=\"{}\" type={} equipped={}",
            channel,
            actor->GetFormID(),
            name,
            objectType,
            equipped ? 1 : 0);
    }

    void AddonBridge::ApplyRemoteOverlaySnapshot(
        RE::Actor* actor,
        std::string_view channel,
        const std::vector<std::string>& chunks)
    {
        if (!actor || actor->IsPlayerRef() || channel.empty() || chunks.empty()) {
            return;
        }

        auto& bridge = RaceMenuOverlayBridge::GetSingleton();
        const bool isOCum = EqualsInsensitive(channel, kOCumChannel);

        OCumOverlayWireSnapshot wire{};
        const std::vector<std::string>* propertyChunks = &chunks;
        if (isOCum) {
            wire = OCumOverlayVisibility::SplitIncomingSnapshot(chunks, 2200);
            propertyChunks = &wire.propertyChunks;
        }

        std::size_t clearedChunks = 0;
        std::size_t staleNodes = 0;

        if (isOCum) {
            // The mirrored OStim scene runs OCum locally for the STR proxy too.
            // Reconcile stale CumOverlays using only real RaceMenu properties;
            // the synthetic live-visibility metadata has already been stripped.
            const auto current = bridge.CaptureMarkedOverlayChunks(
                actor,
                kOCumTextureMarker,
                2200);
            const auto currentNodes = CollectOverlayNodeKeys(current);
            const auto authoritativeNodes = CollectOverlayNodeKeys(*propertyChunks);
            for (const auto& node : currentNodes) {
                if (!authoritativeNodes.contains(node)) {
                    ++staleNodes;
                }
            }

            const auto clearChunks = BuildStaleOverlayClearChunks(
                current,
                *propertyChunks);
            for (const auto& clear : clearChunks) {
                if (clear.empty()) {
                    continue;
                }
                bridge.ApplyRemoteOverlayChunk(actor, channel, clear);
                ++clearedChunks;
            }
        }

        std::size_t appliedChunks = 0;
        for (const auto& chunk : *propertyChunks) {
            if (chunk.empty()) {
                continue;
            }
            bridge.ApplyRemoteOverlayChunk(actor, channel, chunk);
            ++appliedChunks;
        }

        std::size_t visibleEntries = 0;
        if (isOCum) {
            visibleEntries = static_cast<std::size_t>(std::count_if(
                wire.visibility.begin(),
                wire.visibility.end(),
                [](const OCumOverlayVisibilityEntry& entry) {
                    return entry.visible;
                }));

            // Visibility is a live OCum decision, not a RaceMenu override.
            // Apply it only after the normal property snapshot, and relink only
            // owner-visible Body overlays to the proxy's current body geometry.
            OCumOverlayVisibility::ApplyRemoteVisibility(
                actor,
                wire.visibility,
                "owner-snapshot");
        }

        SKSE::log::info(
            "OSTNET ADDON OVR SNAPSHOT channel={} actor={:08X} wireChunks={} propertyChunks={} appliedChunks={} staleNodes={} clearChunks={} visibilityEntries={} visibleEntries={} mode={}",
            channel,
            actor->GetFormID(),
            chunks.size(),
            propertyChunks->size(),
            appliedChunks,
            staleNodes,
            clearedChunks,
            wire.visibility.size(),
            visibleEntries,
            isOCum ? "owner-authoritative-cumoverlays+live-visibility" : "generic");
    }

    void AddonBridge::HandleRemotePacket(
        const std::string& sender,
        std::string_view payload)
    {
        if (payload.starts_with("ADDONOVR|")) {
            const auto channelHex = Field(payload, "channel");
            const auto nameHex = Field(payload, "name");
            const auto seqValue = Field(payload, "seq");
            const auto countValue = Field(payload, "count");
            const auto props = Field(payload, "props");

            if (!channelHex || !nameHex || !seqValue || !countValue || !props) {
                SKSE::log::warn("OSTNET ADDON OVR RX invalid sender={}", sender);
                return;
            }

            std::size_t seq = 0;
            std::size_t count = 0;
            try {
                seq = std::stoull(*seqValue);
                count = std::stoull(*countValue);
            } catch (...) {
                SKSE::log::warn("OSTNET ADDON OVR RX invalid sequence sender={}", sender);
                return;
            }

            if (count == 0 || count > 64 || seq >= count) {
                SKSE::log::warn(
                    "OSTNET ADDON OVR RX sequence out of range sender={} seq={} count={}",
                    sender,
                    seq,
                    count);
                return;
            }

            const auto channel = HexDecode(*channelHex);
            const auto name = HexDecode(*nameHex);
            if (!channel || !name || !IsSafeAddonToken(*channel) || name->empty()) {
                SKSE::log::warn("OSTNET ADDON OVR RX decode failed sender={}", sender);
                return;
            }

            auto* actor = ActorResolver::GetSingleton().ResolveRemotePlayerByName(*name);
            if (!actor) {
                SKSE::log::warn(
                    "OSTNET ADDON OVR RX unresolved sender={} channel={} name=\"{}\"",
                    sender,
                    *channel,
                    *name);
                return;
            }

            const bool isOCum = EqualsInsensitive(*channel, kOCumChannel);
            const std::string cacheKey = isOCum ? "OCum" : *channel;
            bool complete = false;
            std::vector<std::string> completeSnapshot;

            {
                std::scoped_lock lock(_stateMutex);
                auto& cached = _remoteOverlays[actor->GetFormID()][cacheKey];

                // ADDONOVR is ordered. seq=0 starts a new snapshot even if the
                // chunk count matches the previous one.
                if (seq == 0 ||
                    cached.expectedCount != count ||
                    cached.chunks.size() != count ||
                    cached.received.size() != count) {
                    cached.expectedCount = count;
                    cached.chunks.assign(count, {});
                    cached.received.assign(count, false);
                }

                cached.chunks[seq] = *props;
                cached.received[seq] = true;
                complete = std::all_of(
                    cached.received.begin(),
                    cached.received.end(),
                    [](bool value) { return value; });

                if (complete) {
                    cached.appliedChunks = cached.chunks;
                    completeSnapshot = cached.appliedChunks;
                }
            }

            if (isOCum) {
                if (complete) {
                    ApplyRemoteOverlaySnapshot(actor, *channel, completeSnapshot);
                }
            } else {
                // Preserve the generic low-latency behavior for other addons.
                RaceMenuOverlayBridge::GetSingleton()
                    .ApplyRemoteOverlayChunk(actor, *channel, *props);
            }

            SKSE::log::info(
                "OSTNET ADDON OVR RX sender={} channel={} name=\"{}\" actor={:08X} seq={}/{} complete={} authoritative={}",
                sender,
                *channel,
                *name,
                actor->GetFormID(),
                seq + 1,
                count,
                complete ? 1 : 0,
                isOCum ? 1 : 0);
            return;
        }

        if (payload.starts_with("ADDONOBJ|")) {
            const auto channelHex = Field(payload, "channel");
            const auto nameHex = Field(payload, "name");
            const auto typeHex = Field(payload, "type");
            const auto equippedValue = Field(payload, "equipped");

            if (!channelHex || !nameHex || !typeHex || !equippedValue) {
                SKSE::log::warn("OSTNET ADDON OBJ RX invalid sender={}", sender);
                return;
            }

            const auto channel = HexDecode(*channelHex);
            const auto name = HexDecode(*nameHex);
            const auto objectType = HexDecode(*typeHex);
            if (!channel || !name || !objectType ||
                !IsSafeAddonToken(*channel) ||
                !IsSafeAddonToken(*objectType) ||
                name->empty()) {
                SKSE::log::warn("OSTNET ADDON OBJ RX decode failed sender={}", sender);
                return;
            }

            auto* actor = ActorResolver::GetSingleton().ResolveRemotePlayerByName(*name);
            if (!actor) {
                SKSE::log::warn(
                    "OSTNET ADDON OBJ RX unresolved sender={} channel={} name=\"{}\" type={}",
                    sender,
                    *channel,
                    *name,
                    *objectType);
                return;
            }

            const bool equipped = *equippedValue == "1";
            {
                std::scoped_lock lock(_stateMutex);
                const auto key = fmt::format("{}|{}", *channel, *objectType);
                _remoteObjects[actor->GetFormID()][key] = CachedObjectState{
                    *channel,
                    *objectType,
                    equipped };
            }

            const bool dispatched = PapyrusAnimationBridge::GetSingleton()
                .SetOStimObjectState(actor, *objectType, equipped);

            SKSE::log::info(
                "OSTNET ADDON OBJ RX sender={} channel={} name=\"{}\" actor={:08X} type={} equipped={} dispatched={}",
                sender,
                *channel,
                *name,
                actor->GetFormID(),
                *objectType,
                equipped ? 1 : 0,
                dispatched ? 1 : 0);
            return;
        }
    }

    void AddonBridge::ScheduleRemoteStateReapply(
        RE::Actor* actor,
        std::string_view reason)
    {
        if (!actor || actor->IsPlayerRef()) {
            return;
        }

        const auto actorFormID = actor->GetFormID();
        const std::string reasonCopy(reason);

        const auto schedule =
            [this, actorFormID, reasonCopy](
                std::chrono::milliseconds delay,
                const char* phase) {
                std::thread(
                    [this,
                     actorFormID,
                     reasonCopy,
                     delay,
                     phase = std::string(phase)]() {
                        std::this_thread::sleep_for(delay);

                        auto* tasks = SKSE::GetTaskInterface();
                        if (!tasks) {
                            return;
                        }

                        tasks->AddTask(
                            [this, actorFormID, reasonCopy, phase]() {
                                auto* form = RE::TESForm::LookupByID(actorFormID);
                                auto* actor2 = form ? form->As<RE::Actor>() : nullptr;
                                if (!actor2 || actor2->IsPlayerRef()) {
                                    return;
                                }

                                std::unordered_map<std::string, CachedOverlayState> overlays;
                                std::unordered_map<std::string, CachedObjectState> objects;
                                {
                                    std::scoped_lock lock(_stateMutex);
                                    if (const auto it = _remoteOverlays.find(actorFormID);
                                        it != _remoteOverlays.end()) {
                                        overlays = it->second;
                                    }
                                    if (const auto it = _remoteObjects.find(actorFormID);
                                        it != _remoteObjects.end()) {
                                        objects = it->second;
                                    }
                                }

                                std::size_t overlayChunks = 0;
                                for (const auto& [channel, state] : overlays) {
                                    const auto& snapshot = !state.appliedChunks.empty() ?
                                        state.appliedChunks : state.chunks;
                                    ApplyRemoteOverlaySnapshot(actor2, channel, snapshot);
                                    overlayChunks += std::count_if(
                                        snapshot.begin(),
                                        snapshot.end(),
                                        [](const std::string& chunk) { return !chunk.empty(); });
                                }

                                std::size_t objectCount = 0;
                                for (const auto& [_, state] : objects) {
                                    PapyrusAnimationBridge::GetSingleton()
                                        .SetOStimObjectState(
                                            actor2,
                                            state.objectType,
                                            state.equipped);
                                    ++objectCount;
                                }

                                SKSE::log::info(
                                    "OSTNET ADDON STATE REAPPLY reason={} phase={} actor={:08X} overlayChunks={} objects={}",
                                    reasonCopy,
                                    phase,
                                    actorFormID,
                                    overlayChunks,
                                    objectCount);
                            });
                    }).detach();
            };

        schedule(std::chrono::milliseconds(250), "T250");
        schedule(std::chrono::milliseconds(1100), "T1100");
    }
}
