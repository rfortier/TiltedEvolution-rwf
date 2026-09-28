#include "PCH.h"
#include "SyncService.h"

#include "IEDBridge.h"
#include "IEDSyncTogether/Interface.h"
#include "ProxyResolver.h"
#include "StrTransport.h"
#include "UdpTransport.h"

namespace IEDSyncTogether
{
    namespace
    {
        constexpr std::string_view kGameplayPrefix = "IEDST|v1|";

        const char* TransportModeName(Config::TransportMode mode)
        {
            switch (mode) {
            case Config::TransportMode::kSTR:
                return "STR";
            case Config::TransportMode::kAuto:
                return "Auto";
            case Config::TransportMode::kUDP:
                return "UDP";
            default:
                return "Unknown";
            }
        }
    }

    SyncService& SyncService::GetSingleton()
    {
        static SyncService instance;
        return instance;
    }

    SyncService::~SyncService()
    {
        Stop();
    }

    std::optional<std::string> SyncService::ReadField(
        std::string_view packet,
        std::string_view key)
    {
        const auto needle = fmt::format("{}=", key);
        auto position = packet.find(needle);
        if (position == std::string_view::npos) {
            return std::nullopt;
        }
        position += needle.size();
        auto end = packet.find('|', position);
        if (end == std::string_view::npos) {
            end = packet.size();
        }
        return std::string(packet.substr(position, end - position));
    }

    void SyncService::Start()
    {
        if (_running.exchange(true)) {
            return;
        }

        _config = Config::Load();
        if (!IEDBridge::GetSingleton().IsInstalled()) {
            SKSE::log::critical("Immersive Equipment Displays is not loaded");
            _running.store(false);
            return;
        }

        const auto packetHandler = [](std::string packet) {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([packet = std::move(packet)]() mutable {
                    SyncService::GetSingleton().HandlePacket(std::move(packet));
                });
            }
        };

        bool networkStarted = false;
        if (_config.networkEnabled &&
            _config.transportMode != Config::TransportMode::kUDP) {
            networkStarted = StrTransport::GetSingleton().Start(_config, packetHandler);
        }

        const bool wantsUdp =
            _config.transportMode == Config::TransportMode::kUDP ||
            (_config.transportMode == Config::TransportMode::kAuto && !networkStarted) ||
            (_config.udpFallback && !networkStarted);
        if (_config.networkEnabled && wantsUdp) {
            networkStarted = UdpTransport::GetSingleton().Start(_config, packetHandler);
        }

        if (_config.networkEnabled && !networkStarted) {
            SKSE::log::warn(
                "No network transport started for mode={} udpFallback={}",
                TransportModeName(_config.transportMode),
                _config.udpFallback ? 1 : 0);
        }

        _timer = std::jthread([this](std::stop_token token) { TimerLoop(token); });
        SKSE::log::info(
            "Service started: peer identity uses transport instance id; proxy FormIDs are ephemeral; capture={}ms suppressRemoteNpcDisplays={} transport={} udpFallback={}",
            _config.captureIntervalMs,
            _config.suppressRemoteNpcDisplays ? 1 : 0,
            TransportModeName(_config.transportMode),
            _config.udpFallback ? 1 : 0);
    }

    void SyncService::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }
        if (_timer.joinable()) {
            _timer.request_stop();
            _timer.join();
        }
        StrTransport::GetSingleton().Stop();
        UdpTransport::GetSingleton().Stop();
    }

    void SyncService::Reset()
    {
        for (const auto formID : _blockedProxies) {
            if (auto* form = RE::TESForm::LookupByID(formID)) {
                if (auto* actor = form->As<RE::Actor>()) {
                    IEDBridge::GetSingleton().SetActorBlocked(actor, false);
                }
            }
        }
        _blockedProxies.clear();
        {
            std::scoped_lock lock(_snapshotMutex);
            _remoteSnapshots.clear();
        }
        _hasLocalSlots = false;
        _capturePending.store(false);
    }

    void SyncService::TimerLoop(std::stop_token token)
    {
        while (!token.stop_requested() && _running.load()) {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([]() { SyncService::GetSingleton().Tick(); });
            }

            const auto interval = std::chrono::milliseconds(_config.captureIntervalMs);
            auto elapsed = std::chrono::milliseconds(0);
            while (elapsed < interval && !token.stop_requested() && _running.load()) {
                constexpr auto slice = std::chrono::milliseconds(100);
                std::this_thread::sleep_for(slice);
                elapsed += slice;
            }
        }
    }

    void SyncService::Tick()
    {
        if (!_running.load()) {
            return;
        }

        RefreshProxyMitigation();

        bool expected = false;
        if (!_capturePending.compare_exchange_strong(expected, true)) {
            return;
        }

        const bool requested = IEDBridge::GetSingleton().CapturePlayerSlots(
            [this](SlotState slots) {
                _capturePending.store(false);
                OnLocalCapture(std::move(slots));
            });
        if (!requested) {
            _capturePending.store(false);
        }
    }

    void SyncService::OnLocalCapture(SlotState slots)
    {
        const auto now = std::chrono::steady_clock::now();
        const bool changed = !_hasLocalSlots || slots != _localSlots;
        const bool resendDue = !_hasLocalSlots ||
            now - _lastSend >= std::chrono::milliseconds(_config.resendIntervalMs);
        if (!changed && !resendDue) {
            return;
        }

        if (changed) {
            _localSlots = std::move(slots);
            _hasLocalSlots = true;
            ++_revision;
        }

        const auto payload = fmt::format(
            "STATE|rev={}|slots={}",
            _revision,
            EncodeSlots(_localSlots));
        SendNetworkPayload(payload);
        _lastSend = now;

        const auto visible = std::count_if(
            _localSlots.begin(),
            _localSlots.end(),
            [](const auto& slot) { return slot.has_value(); });
        SKSE::log::info(
            "Local IED state: revision={} displayedSlots={} changed={}",
            _revision,
            visible,
            changed ? 1 : 0);
    }

    void SyncService::SendNetworkPayload(std::string_view payload)
    {
        if (StrTransport::GetSingleton().IsRunning()) {
            StrTransport::GetSingleton().Send(payload);
            return;
        }
        if (UdpTransport::GetSingleton().IsRunning()) {
            UdpTransport::GetSingleton().Send(payload);
        }
    }

    void SyncService::HandlePacket(std::string packet)
    {
        if (!packet.starts_with(kGameplayPrefix) || packet.find("|STATE|") == std::string::npos) {
            return;
        }

        const auto instanceID = ReadField(packet, "id");
        const auto encodedSender = ReadField(packet, "from");
        const auto revisionText = ReadField(packet, "rev");
        const auto slotsText = ReadField(packet, "slots");
        if (!instanceID || instanceID->empty() || !encodedSender || !revisionText || !slotsText) {
            return;
        }

        const auto sender = HexDecode(*encodedSender);
        const auto slots = DecodeSlots(*slotsText);
        if (!sender || sender->empty() || !slots) {
            SKSE::log::warn("Rejected malformed remote IED state");
            return;
        }

        std::uint64_t revision = 0;
        try {
            revision = std::stoull(*revisionText);
        } catch (...) {
            return;
        }

        RE::FormID currentProxy = 0;
        if (auto* proxy = FindRemotePlayerProxy(*sender)) {
            currentProxy = proxy->GetFormID();
        }

        bool characterChanged = false;
        {
            std::scoped_lock lock(_snapshotMutex);
            auto& snapshot = _remoteSnapshots[*instanceID];
            characterChanged = !snapshot.characterName.empty() &&
                               snapshot.characterName != *sender;
            if (characterChanged) {
                snapshot = RemoteSnapshot{};
            }
            snapshot.characterName = *sender;

            if (revision < snapshot.revision) {
                return;
            }

            snapshot.slots = *slots;
            snapshot.revision = revision;
            snapshot.lastProxy = currentProxy;
        }

        const auto visible = std::count_if(
            slots->begin(),
            slots->end(),
            [](const auto& slot) { return slot.has_value(); });
        SKSE::log::info(
            "Remote IED state stored: peer={} character=\"{}\" proxy={} revision={} slots={}{}",
            *instanceID,
            *sender,
            currentProxy ? fmt::format("{:08X}", currentProxy) : std::string("unresolved"),
            revision,
            visible,
            characterChanged ? " characterChanged=1" : "");
    }

    void SyncService::RefreshProxyMitigation()
    {
        std::unordered_set<RE::FormID> current;

        {
            std::scoped_lock lock(_snapshotMutex);
            for (auto& [peerID, snapshot] : _remoteSnapshots) {
                RE::FormID newProxy = 0;
                if (!snapshot.characterName.empty()) {
                    if (auto* proxy = FindRemotePlayerProxy(snapshot.characterName)) {
                        newProxy = proxy->GetFormID();
                    }
                }

                const auto previousProxy = snapshot.lastProxy;
                snapshot.lastProxy = newProxy;
                if (newProxy != 0) {
                    current.insert(newProxy);
                }

                if (previousProxy == newProxy) {
                    continue;
                }

                if (previousProxy == 0 && newProxy != 0) {
                    SKSE::log::info(
                        "Peer proxy bound: peer={} character=\"{}\" proxy={:08X}",
                        peerID,
                        snapshot.characterName,
                        newProxy);
                } else if (previousProxy != 0 && newProxy == 0) {
                    SKSE::log::info(
                        "Peer proxy disappeared: peer={} character=\"{}\" oldProxy={:08X}; retaining snapshot",
                        peerID,
                        snapshot.characterName,
                        previousProxy);
                } else {
                    SKSE::log::info(
                        "Peer proxy rebound: peer={} character=\"{}\" {:08X}->{:08X}",
                        peerID,
                        snapshot.characterName,
                        previousProxy,
                        newProxy);
                }
            }
        }

        for (const auto formID : current) {
            if (!_config.suppressRemoteNpcDisplays ||
                !_blockedProxies.insert(formID).second) {
                continue;
            }

            if (auto* form = RE::TESForm::LookupByID(formID)) {
                if (auto* actor = form->As<RE::Actor>()) {
                    IEDBridge::GetSingleton().SetActorBlocked(actor, true);
                    const auto* proxyName = actor->GetName();
                    SKSE::log::info(
                        "Suppressed IED NPC display on reboundable proxy {:08X} \"{}\"",
                        formID,
                        proxyName ? proxyName : "");
                }
            }
        }

        for (auto iterator = _blockedProxies.begin(); iterator != _blockedProxies.end();) {
            if (!_config.suppressRemoteNpcDisplays || !current.contains(*iterator)) {
                if (auto* form = RE::TESForm::LookupByID(*iterator)) {
                    if (auto* actor = form->As<RE::Actor>()) {
                        IEDBridge::GetSingleton().SetActorBlocked(actor, false);
                    }
                }
                iterator = _blockedProxies.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    std::uint32_t SyncService::QueryRemoteSlot(
        RE::FormID actorFormID,
        std::uint32_t slotIndex,
        RE::FormID& outFormID) const
    {
        outFormID = 0;
        if (slotIndex >= IEDST::kSlotCount) {
            return static_cast<std::uint32_t>(IEDST::SlotOverrideResult::kNotRemote);
        }

        auto* form = RE::TESForm::LookupByID(actorFormID);
        auto* actor = form ? form->As<RE::Actor>() : nullptr;
        if (!IsLikelyRemotePlayerProxy(actor)) {
            return static_cast<std::uint32_t>(IEDST::SlotOverrideResult::kNotRemote);
        }

        std::optional<FormIdentity> identity;
        {
            std::scoped_lock lock(_snapshotMutex);
            const auto iterator = std::find_if(
                _remoteSnapshots.begin(),
                _remoteSnapshots.end(),
                [actorFormID](const auto& entry) {
                    return entry.second.lastProxy == actorFormID;
                });
            if (iterator == _remoteSnapshots.end()) {
                return static_cast<std::uint32_t>(IEDST::SlotOverrideResult::kNotRemote);
            }
            identity = iterator->second.slots[slotIndex];
        }

        if (!identity) {
            return static_cast<std::uint32_t>(IEDST::SlotOverrideResult::kEmpty);
        }
        if (auto* resolved = ResolveFormIdentity(*identity)) {
            outFormID = resolved->GetFormID();
            return static_cast<std::uint32_t>(IEDST::SlotOverrideResult::kForm);
        }
        return static_cast<std::uint32_t>(IEDST::SlotOverrideResult::kEmpty);
    }
}
