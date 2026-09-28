#include "PCH.h"
#include "StrMessagingTransport.h"
#include "Protocol.h"

#include <cstring>

namespace TradeTogether
{
    namespace
    {
        constexpr char kChannel[] = "tradetogether.offer.v1";
        constexpr std::string_view kGameplayPrefix = "TTNET|v1|";

        std::string ReadLocalPlayerName()
        {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                const auto* name = player->GetName();
                if (name && *name) {
                    return name;
                }
            }
            return "Player";
        }

        std::string ReadActorName(RE::Actor* a_actor)
        {
            if (!a_actor) {
                return {};
            }
            const auto* name = a_actor->GetDisplayFullName();
            if (!name || !*name) {
                name = a_actor->GetName();
            }
            return name && *name ? name : std::string{};
        }

        bool SameChannel(const STRPM::Message& a_message)
        {
            return a_message.channel && std::strcmp(a_message.channel, kChannel) == 0;
        }

        std::string_view BackendName(STRPM::RuntimeBackend a_backend)
        {
            switch (a_backend) {
            case STRPM::RuntimeBackend::kUdp: return "UDP";
            case STRPM::RuntimeBackend::kStrBridge: return "STRBridge";
            case STRPM::RuntimeBackend::kNone:
            default: return "None";
            }
        }
    }

    StrMessagingTransport& StrMessagingTransport::GetSingleton()
    {
        static StrMessagingTransport singleton;
        return singleton;
    }

    StrMessagingTransport::~StrMessagingTransport()
    {
        Stop();
    }

    std::string StrMessagingTransport::GetLocalPlayerName() const
    {
        return ReadLocalPlayerName();
    }

    std::optional<std::string> StrMessagingTransport::GetMostRecentPeerName()
    {
        std::scoped_lock lock(_peerMutex);
        return _mostRecentPeerName;
    }

    void STRPM_CALL StrMessagingTransport::OnMessage(const STRPM::Message* a_message, void* a_userData)
    {
        // STRPluginMessagingBridge currently invokes this callback from its
        // TransportService::OnConsume vectored exception handler. Keep this
        // callback strictly allocation-free / lock-free: only copy into a
        // preallocated SPSC ring and return immediately.
        if (a_userData) {
            static_cast<StrMessagingTransport*>(a_userData)->EnqueueInboundMessage(a_message);
        }
    }

    bool StrMessagingTransport::EnqueueInboundMessage(const STRPM::Message* a_message) noexcept
    {
        if (!a_message || !a_message->data || a_message->size == 0 ||
            a_message->size > STRPM::kMaxPayloadBytes) {
            return false;
        }

        const auto write = _inboundWrite.load(std::memory_order_relaxed);
        const auto next = (write + 1) % kInboundQueueCapacity;
        if (next == _inboundRead.load(std::memory_order_acquire)) {
            _inboundDropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        auto& slot = _inboundQueue[write];
        slot.size = a_message->size;
        std::memcpy(slot.payload.data(), a_message->data, a_message->size);
        slot.senderConnectionID = a_message->sender.connectionID;
        slot.flags = a_message->flags;
        slot.sequence = a_message->sequence;

        slot.senderName.fill('\0');
        if (a_message->sender.displayName) {
            for (std::size_t i = 0; i + 1 < slot.senderName.size(); ++i) {
                const auto c = a_message->sender.displayName[i];
                if (c == '\0') {
                    break;
                }
                slot.senderName[i] = c;
            }
        }

        _inboundWrite.store(next, std::memory_order_release);
        return true;
    }

    void StrMessagingTransport::DispatchLoop(std::stop_token a_stopToken)
    {
        spdlog::info("TradeTogether STRPM deferred receive dispatcher started");

        while (!a_stopToken.stop_requested()) {
            const auto read = _inboundRead.load(std::memory_order_relaxed);
            if (read == _inboundWrite.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            HandleInboundMessage(_inboundQueue[read]);
            _inboundRead.store(
                (read + 1) % kInboundQueueCapacity,
                std::memory_order_release);
        }

        const auto dropped = _inboundDropped.load(std::memory_order_relaxed);
        spdlog::info(
            "TradeTogether STRPM deferred receive dispatcher stopped: dropped={}",
            dropped);
    }

    void StrMessagingTransport::HandleInboundMessage(const InboundMessage& a_message)
    {
        STRPM::Message message{};
        message.channel = kChannel;
        message.data = a_message.payload.data();
        message.size = a_message.size;
        message.sender.connectionID = a_message.senderConnectionID;
        message.sender.displayName = a_message.senderName[0] != '\0' ?
            a_message.senderName.data() : nullptr;
        message.sender.isHost = false;
        message.flags = a_message.flags;
        message.sequence = a_message.sequence;
        HandleMessage(&message);
    }

    void STRPM_CALL StrMessagingTransport::OnProxyMapping(const STRPM::ProxyMappingEvent* a_event, void* a_userData)
    {
        if (a_userData) {
            static_cast<StrMessagingTransport*>(a_userData)->HandleProxyMapping(a_event);
        }
    }

    void StrMessagingTransport::HandleProxyMapping(const STRPM::ProxyMappingEvent* a_event)
    {
        if (!a_event) {
            return;
        }

        std::scoped_lock lock(_proxyMutex);
        if (a_event->type == STRPM::ProxyMappingEventType::kCleared) {
            _proxyConnections.clear();
            _namedConnections.clear();
            spdlog::info("TradeTogether STRPM proxy mappings cleared");
            return;
        }

        if (a_event->oldFormID != STRPM::kInvalidProxyFormID) {
            _proxyConnections.erase(a_event->oldFormID);
        }
        if (a_event->type == STRPM::ProxyMappingEventType::kRemoved ||
            a_event->type == STRPM::ProxyMappingEventType::kUpdated) {
            std::erase_if(_namedConnections, [&](const auto& a_entry) {
                return a_entry.second == a_event->connectionID;
            });
        }
        if (a_event->newFormID != STRPM::kInvalidProxyFormID && a_event->connectionID != 0) {
            _proxyConnections[a_event->newFormID] = a_event->connectionID;
            spdlog::info(
                "TradeTogether STRPM proxy mapped: connection={} form={:08X}",
                a_event->connectionID,
                a_event->newFormID);
        } else if (a_event->type == STRPM::ProxyMappingEventType::kRemoved) {
            spdlog::info(
                "TradeTogether STRPM proxy removed: connection={} form={:08X}",
                a_event->connectionID,
                a_event->oldFormID);
        }
    }

    void StrMessagingTransport::RefreshIdentity()
    {
        if (!_api) {
            return;
        }
        const auto localName = GetLocalPlayerName();
        if (_api->setLocalDisplayName) {
            const auto result = _api->setLocalDisplayName(localName.c_str());
            if (result != STRPM::Result::kOk) {
                spdlog::debug("TradeTogether STRPM setLocalDisplayName: {}", STRPM::ResultToString(result));
            }
        }
        if (_api->getLocalConnectionID) {
            STRPM::ConnectionID connectionID = 0;
            const auto result = _api->getLocalConnectionID(&connectionID);
            if (result == STRPM::Result::kOk && connectionID != 0) {
                _localConnectionID = connectionID;
            }
        }
    }

    std::optional<STRPM::ConnectionID> StrMessagingTransport::ResolveConnectionIDForPlayerName(std::string_view a_playerName)
    {
        if (a_playerName.empty()) {
            return std::nullopt;
        }

        {
            std::scoped_lock lock(_proxyMutex);
            for (const auto& [name, connectionID] : _namedConnections) {
                if (Protocol::EqualsInsensitive(name, a_playerName)) {
                    return connectionID;
                }
            }
        }

        std::vector<std::pair<STRPM::ProxyFormID, STRPM::ConnectionID>> proxies;
        {
            std::scoped_lock lock(_proxyMutex);
            proxies.reserve(_proxyConnections.size());
            for (const auto& entry : _proxyConnections) {
                proxies.push_back(entry);
            }
        }

        for (const auto& [formID, connectionID] : proxies) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID);
            const auto actorName = ReadActorName(actor);
            if (!actorName.empty() && Protocol::EqualsInsensitive(actorName, a_playerName)) {
                std::scoped_lock lock(_proxyMutex);
                _namedConnections[actorName] = connectionID;
                _namedConnections[std::string(a_playerName)] = connectionID;
                spdlog::info(
                    "TradeTogether STRPM resolved player proxy: name=\"{}\" connection={} form={:08X}",
                    actorName,
                    connectionID,
                    formID);
                return connectionID;
            }
        }

        spdlog::info(
            "TradeTogether STRPM target is not a mapped player proxy: name=\"{}\" mappedProxies={}",
            a_playerName,
            proxies.size());
        return std::nullopt;
    }

    bool StrMessagingTransport::Start(const Config& a_config, PacketHandler a_handler)
    {
        if (_running.load()) {
            return true;
        }
        if (!a_config.networkEnabled || !a_handler) {
            return false;
        }

        _api = STRPM::LoadFromModule();
        if (!_api) {
            spdlog::error("TradeTogether STRPM API unavailable: STRPluginMessagingAPI.dll not found or incompatible");
            return false;
        }
        _diagnostics = STRPM::LoadDiagnosticsFromModule();
        _proxyResolver = STRPM::LoadProxyResolverFromModule();
        if (!_proxyResolver || !_proxyResolver->registerListener || !_proxyResolver->unregisterListener) {
            spdlog::error("TradeTogether STRPM proxy resolver unavailable: install a current STRPluginMessagingAPI build");
            _api = nullptr;
            _diagnostics = nullptr;
            _proxyResolver = nullptr;
            return false;
        }

        {
            std::scoped_lock lock(_handlerMutex);
            _handler = std::move(a_handler);
        }
        {
            std::scoped_lock lock(_peerMutex);
            _mostRecentPeerName.reset();
        }
        {
            std::scoped_lock lock(_proxyMutex);
            _proxyConnections.clear();
            _namedConnections.clear();
        }
        _inboundRead.store(0, std::memory_order_relaxed);
        _inboundWrite.store(0, std::memory_order_relaxed);
        _inboundDropped.store(0, std::memory_order_relaxed);

        RefreshIdentity();

        STRPM::ListenerHandle listener{};
        const auto channelResult = _api->registerChannel(kChannel, &StrMessagingTransport::OnMessage, this, &listener);
        if (channelResult != STRPM::Result::kOk) {
            spdlog::error("TradeTogether STRPM registerChannel failed: {}", STRPM::ResultToString(channelResult));
            std::scoped_lock lock(_handlerMutex);
            _handler = {};
            _api = nullptr;
            _diagnostics = nullptr;
            _proxyResolver = nullptr;
            return false;
        }

        const auto proxyResult = _proxyResolver->registerListener(&StrMessagingTransport::OnProxyMapping, this);
        if (proxyResult != STRPM::Result::kOk) {
            spdlog::error("TradeTogether STRPM proxy listener registration failed: {}", STRPM::ResultToString(proxyResult));
            _api->unregisterChannel(listener);
            std::scoped_lock lock(_handlerMutex);
            _handler = {};
            _api = nullptr;
            _diagnostics = nullptr;
            _proxyResolver = nullptr;
            return false;
        }

        _listener = listener;
        _running.store(true);
        _dispatchThread = std::jthread([this](std::stop_token a_stopToken) {
            DispatchLoop(a_stopToken);
        });

        std::size_t proxyCount = 0;
        {
            std::scoped_lock lock(_proxyMutex);
            proxyCount = _proxyConnections.size();
        }
        spdlog::info(
            "TradeTogether STRPM transport started: channel={} player=\"{}\" connection={} mappedProxies={} deferredReceive=1",
            kChannel,
            GetLocalPlayerName(),
            _localConnectionID,
            proxyCount);
        LogRuntimeStatus("startup");
        return true;
    }

    void StrMessagingTransport::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }
        if (_proxyResolver && _proxyResolver->unregisterListener) {
            const auto result = _proxyResolver->unregisterListener(&StrMessagingTransport::OnProxyMapping, this);
            if (result != STRPM::Result::kOk && result != STRPM::Result::kNotAvailable) {
                spdlog::warn("TradeTogether STRPM proxy listener unregister failed: {}", STRPM::ResultToString(result));
            }
        }
        if (_api && _api->unregisterChannel && _listener.value != 0) {
            const auto result = _api->unregisterChannel(_listener);
            if (result != STRPM::Result::kOk && result != STRPM::Result::kChannelNotRegistered) {
                spdlog::warn("TradeTogether STRPM unregisterChannel failed: {}", STRPM::ResultToString(result));
            }
        }
        if (_dispatchThread.joinable()) {
            _dispatchThread.request_stop();
            _dispatchThread.join();
        }
        {
            std::scoped_lock lock(_handlerMutex);
            _handler = {};
        }
        {
            std::scoped_lock lock(_peerMutex);
            _mostRecentPeerName.reset();
        }
        {
            std::scoped_lock lock(_proxyMutex);
            _proxyConnections.clear();
            _namedConnections.clear();
        }
        _listener = {};
        _localConnectionID = 0;
        _api = nullptr;
        _diagnostics = nullptr;
        _proxyResolver = nullptr;
        _inboundRead.store(0, std::memory_order_relaxed);
        _inboundWrite.store(0, std::memory_order_relaxed);
        spdlog::info("TradeTogether STRPM transport stopped");
    }

    bool StrMessagingTransport::SendTo(std::string_view a_playerName, std::string_view a_payload)
    {
        if (!_running.load() || !_api || !_api->send || a_playerName.empty() || a_payload.empty()) {
            return false;
        }

        const auto connectionID = ResolveConnectionIDForPlayerName(a_playerName);
        if (!connectionID || *connectionID == 0) {
            return false;
        }

        RefreshIdentity();
        const auto packet = fmt::format(
            "TTNET|v1|id={}|from={}|{}",
            _localConnectionID,
            Protocol::HexEncode(GetLocalPlayerName()),
            a_payload);
        if (packet.size() > STRPM::kMaxPayloadBytes) {
            spdlog::warn("TradeTogether STRPM packet too large: bytes={}", packet.size());
            return false;
        }

        const STRPM::Target playerTarget{ STRPM::TargetKind::kPlayer, *connectionID, nullptr };
        constexpr auto flags = STRPM::kMessageReliable | STRPM::kMessageOrdered;
        const auto result = _api->send(kChannel, playerTarget, packet.data(), packet.size(), flags);

        if (result == STRPM::Result::kTargetNotFound) {
            spdlog::info(
                "TradeTogether STRPM target connection not found: name=\"{}\" connection={}",
                a_playerName,
                *connectionID);
            return false;
        }

        if (result != STRPM::Result::kOk) {
            spdlog::warn(
                "TradeTogether STRPM send failed: target=\"{}\" connection={} result={}",
                a_playerName,
                *connectionID,
                STRPM::ResultToString(result));
            LogRuntimeStatus("send failure");
            return false;
        }

        spdlog::info(
            "TradeTogether STRPM packet sent: target=\"{}\" connection={} bytes={}",
            a_playerName,
            *connectionID,
            packet.size());
        return true;
    }

    std::optional<STRPM::RuntimeStatus> StrMessagingTransport::QueryRuntimeStatus() const
    {
        if (!_diagnostics || !_diagnostics->getRuntimeStatus) {
            return std::nullopt;
        }
        STRPM::RuntimeStatus status{};
        const auto result = _diagnostics->getRuntimeStatus(&status);
        if (result != STRPM::Result::kOk || status.version != STRPM::kDiagnosticsVersion) {
            return std::nullopt;
        }
        return status;
    }

    void StrMessagingTransport::LogRuntimeStatus(std::string_view a_context) const
    {
        const auto status = QueryRuntimeStatus();
        if (!status) {
            spdlog::info("TradeTogether STRPM diagnostics unavailable ({})", a_context);
            return;
        }
        std::size_t proxyCount = 0;
        {
            std::scoped_lock lock(_proxyMutex);
            proxyCount = _proxyConnections.size();
        }
        spdlog::info(
            "TradeTogether STRPM status {}: backend={} bridgeAvailable={} bridgeActive={} knownPeers={} configuredPeers={} mappedProxies={}",
            a_context,
            BackendName(status->activeBackend),
            status->strBridgeAvailable,
            status->strBridgeActive,
            status->knownPeerCount,
            status->configuredPeerCount,
            proxyCount);
    }

    void StrMessagingTransport::HandleMessage(const STRPM::Message* a_message)
    {
        if (!_running.load() || !a_message || !SameChannel(*a_message) || !a_message->data || a_message->size == 0 || a_message->size > STRPM::kMaxPayloadBytes) {
            return;
        }

        std::string packet(static_cast<const char*>(a_message->data), a_message->size);
        if (!packet.starts_with(kGameplayPrefix)) {
            return;
        }

        std::optional<std::string> peerName;
        if (const auto encodedFrom = Protocol::ReadField(packet, "from")) {
            peerName = Protocol::HexDecode(*encodedFrom);
        }
        if ((!peerName || peerName->empty()) && a_message->sender.displayName && *a_message->sender.displayName) {
            peerName = std::string(a_message->sender.displayName);
        }
        if (peerName && !peerName->empty() && !Protocol::EqualsInsensitive(*peerName, GetLocalPlayerName())) {
            {
                std::scoped_lock lock(_peerMutex);
                _mostRecentPeerName = *peerName;
            }
            if (a_message->sender.connectionID != 0) {
                std::scoped_lock lock(_proxyMutex);
                _namedConnections[*peerName] = a_message->sender.connectionID;
            }
        }

        PacketHandler handler;
        {
            std::scoped_lock lock(_handlerMutex);
            handler = _handler;
        }
        if (handler) {
            handler(std::move(packet));
        }
    }
}
