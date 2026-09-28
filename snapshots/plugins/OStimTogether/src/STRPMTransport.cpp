#include "PCH.h"
#include "STRPMTransport.h"

#include "ActorResolver.h"
#include "CoopSessionManager.h"

namespace OStimTogether
{
    namespace
    {
        constexpr wchar_t kModuleName[] = L"STRPluginMessagingAPI.dll";
        constexpr char kChannel[] = "ostimtogether";

        std::string SafeSenderName(const STRPMApi::Message& message)
        {
            std::string result =
                message.sender.displayName ? message.sender.displayName : "";

            for (auto& ch : result) {
                if (ch == '|' || ch == '\r' || ch == '\n') {
                    ch = '_';
                }
            }

            if (result.empty()) {
                result = fmt::format(
                    "STRPM-{}",
                    message.sender.connectionID);
            }

            return result;
        }
    }

    STRPMTransport& STRPMTransport::GetSingleton()
    {
        static STRPMTransport instance;
        return instance;
    }

    STRPMTransport::~STRPMTransport()
    {
        Stop();
    }

    const char* STRPMTransport::ResultName(
        STRPMApi::Result result) noexcept
    {
        switch (result) {
        case STRPMApi::Result::kOk:
            return "ok";
        case STRPMApi::Result::kNotAvailable:
            return "not-available";
        case STRPMApi::Result::kUnsupportedVersion:
            return "unsupported-version";
        case STRPMApi::Result::kInvalidArgument:
            return "invalid-argument";
        case STRPMApi::Result::kNotConnected:
            return "not-connected";
        case STRPMApi::Result::kChannelAlreadyRegistered:
            return "channel-already-registered";
        case STRPMApi::Result::kChannelNotRegistered:
            return "channel-not-registered";
        case STRPMApi::Result::kPayloadTooLarge:
            return "payload-too-large";
        case STRPMApi::Result::kRateLimited:
            return "rate-limited";
        case STRPMApi::Result::kTransportError:
            return "transport-error";
        case STRPMApi::Result::kTargetNotFound:
            return "target-not-found";
        default:
            return "unknown";
        }
    }

    const char* STRPMTransport::MappingEventName(
        STRPMApi::ProxyMappingEventType type) noexcept
    {
        switch (type) {
        case STRPMApi::ProxyMappingEventType::kAdded:
            return "added";
        case STRPMApi::ProxyMappingEventType::kUpdated:
            return "updated";
        case STRPMApi::ProxyMappingEventType::kRemoved:
            return "removed";
        case STRPMApi::ProxyMappingEventType::kCleared:
            return "cleared";
        default:
            return "unknown";
        }
    }

    const char* STRPMTransport::BackendName(
        STRPMApi::RuntimeBackend backend) noexcept
    {
        switch (backend) {
        case STRPMApi::RuntimeBackend::kNone:
            return "none";
        case STRPMApi::RuntimeBackend::kUdp:
            return "udp";
        case STRPMApi::RuntimeBackend::kStrBridge:
            return "str-bridge";
        default:
            return "unknown";
        }
    }

    void STRPMTransport::LogRuntimeStatus(std::string_view reason) const
    {
        if (!_diagnostics || !_diagnostics->getRuntimeStatus) {
            SKSE::log::info(
                "OSTNET STRPM STATUS reason={} diagnostics=unavailable",
                reason);
            return;
        }

        STRPMApi::RuntimeStatus status{};
        const auto result =
            _diagnostics->getRuntimeStatus(&status);

        if (result != STRPMApi::Result::kOk) {
            SKSE::log::warn(
                "OSTNET STRPM STATUS reason={} result={}",
                reason,
                ResultName(result));
            return;
        }

        SKSE::log::info(
            "OSTNET STRPM STATUS reason={} backend={} bridgeAvailable={} bridgeActive={} knownPeers={} configuredPeers={}",
            reason,
            BackendName(status.activeBackend),
            status.strBridgeAvailable,
            status.strBridgeActive,
            status.knownPeerCount,
            status.configuredPeerCount);
    }

    bool STRPMTransport::Start()
    {
        if (_running.load()) {
            return true;
        }

        const auto module = GetModuleHandleW(kModuleName);
        if (!module) {
            SKSE::log::warn(
                "OSTNET STRPM unavailable: STRPluginMessagingAPI.dll is not loaded; synchronization disabled");
            return false;
        }

        const auto queryInterface =
            reinterpret_cast<STRPMApi::QueryInterfaceFn>(
                GetProcAddress(
                    module,
                    STRPMApi::kQueryInterfaceExportName));

        if (!queryInterface) {
            SKSE::log::warn(
                "OSTNET STRPM unavailable: missing export {}",
                STRPMApi::kQueryInterfaceExportName);
            return false;
        }

        const STRPMApi::Interface* api = nullptr;
        const auto apiResult =
            queryInterface(STRPMApi::kInterfaceVersion, &api);

        if (apiResult != STRPMApi::Result::kOk ||
            !api ||
            !api->registerChannel ||
            !api->send) {
            SKSE::log::warn(
                "OSTNET STRPM interface load failed result={}",
                ResultName(apiResult));
            return false;
        }

        const STRPMApi::DiagnosticsInterface* diagnostics = nullptr;
        const auto queryDiagnostics =
            reinterpret_cast<STRPMApi::QueryDiagnosticsFn>(
                GetProcAddress(
                    module,
                    STRPMApi::kQueryDiagnosticsExportName));

        if (queryDiagnostics) {
            const auto diagnosticsResult =
                queryDiagnostics(
                    STRPMApi::kDiagnosticsVersion,
                    &diagnostics);
            if (diagnosticsResult != STRPMApi::Result::kOk) {
                diagnostics = nullptr;
            }
        }

        const STRPMApi::ProxyResolverInterface* resolver = nullptr;
        const auto queryResolver =
            reinterpret_cast<STRPMApi::QueryProxyResolverFn>(
                GetProcAddress(
                    module,
                    STRPMApi::kQueryProxyResolverExportName));

        if (queryResolver) {
            const auto resolverResult =
                queryResolver(
                    STRPMApi::kProxyResolverVersion,
                    &resolver);

            if (resolverResult != STRPMApi::Result::kOk) {
                resolver = nullptr;
                SKSE::log::warn(
                    "OSTNET STRPM ProxyResolver unavailable result={}",
                    ResultName(resolverResult));
            }
        }

        STRPMApi::ListenerHandle listener{};
        const auto registerResult =
            api->registerChannel(
                kChannel,
                &STRPMTransport::OnMessage,
                this,
                &listener);

        if (registerResult != STRPMApi::Result::kOk) {
            SKSE::log::warn(
                "OSTNET STRPM register channel failed channel={} result={}",
                kChannel,
                ResultName(registerResult));
            return false;
        }

        _api = api;
        _diagnostics = diagnostics;
        _resolver = resolver;
        _listener = listener;
        _resolverListenerRegistered = false;

        {
            std::scoped_lock lock(_proxyMutex);
            _proxyByConnection.clear();
            _connectionByProxy.clear();
        }

        if (_resolver && _resolver->registerListener) {
            const auto result =
                _resolver->registerListener(
                    &STRPMTransport::OnProxyMapping,
                    this);
            _resolverListenerRegistered =
                result == STRPMApi::Result::kOk;

            if (!_resolverListenerRegistered) {
                SKSE::log::warn(
                    "OSTNET STRPM ProxyResolver listener registration failed result={}",
                    ResultName(result));
            }
        }

        if (_api->setLocalDisplayName) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                const auto* name = player->GetName();
                if (name && *name) {
                    _api->setLocalDisplayName(name);
                }
            }
        }

        _running.store(true);

        SKSE::log::info(
            "OSTNET STRPM READY channel={} apiVersion={} diagnostics={} proxyResolver={} resolverListener={}",
            kChannel,
            _api->version,
            _diagnostics ? 1 : 0,
            _resolver ? 1 : 0,
            _resolverListenerRegistered ? 1 : 0);

        LogRuntimeStatus("start");
        return true;
    }

    void STRPMTransport::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_resolver &&
            _resolverListenerRegistered &&
            _resolver->unregisterListener) {
            _resolver->unregisterListener(
                &STRPMTransport::OnProxyMapping,
                this);
        }

        if (_api &&
            _listener.value != 0 &&
            _api->unregisterChannel) {
            _api->unregisterChannel(_listener);
        }

        _resolverListenerRegistered = false;
        _listener = {};
        _resolver = nullptr;
        _diagnostics = nullptr;
        _api = nullptr;

        {
            std::scoped_lock lock(_proxyMutex);
            _proxyByConnection.clear();
            _connectionByProxy.clear();
        }

        SKSE::log::info("OSTNET STRPM stopped");
    }

    bool STRPMTransport::SendRaw(
        STRPMApi::Target target,
        std::string_view payload)
    {
        if (!_running.load() ||
            !_api ||
            !_api->send ||
            payload.empty()) {
            return false;
        }

        const auto result =
            _api->send(
                kChannel,
                target,
                payload.data(),
                payload.size(),
                STRPMApi::kMessageReliable |
                    STRPMApi::kMessageOrdered);

        if (result != STRPMApi::Result::kOk) {
            SKSE::log::warn(
                "OSTNET STRPM TX failed result={} targetKind={} targetConnection={} bytes={} transport=STRPM-only",
                ResultName(result),
                static_cast<std::uint32_t>(target.kind),
                target.connectionID,
                payload.size());
            LogRuntimeStatus("tx-failed");
            return false;
        }

        SKSE::log::info(
            "OSTNET STRPM TX targetKind={} targetConnection={} bytes={} {}",
            static_cast<std::uint32_t>(target.kind),
            target.connectionID,
            payload.size(),
            payload);
        return true;
    }

    bool STRPMTransport::Send(std::string_view payload)
    {
        if (payload.empty()) {
            return false;
        }

        if (CoopSessionManager::GetSingleton().
                InterceptOutgoing(payload)) {
            return true;
        }

        STRPMApi::Target target{};
        target.kind = STRPMApi::TargetKind::kAllPlayers;
        return SendRaw(target, payload);
    }

    bool STRPMTransport::SendTo(
        STRPMApi::ConnectionID connectionID,
        std::string_view payload)
    {
        if (connectionID == 0 || payload.empty()) {
            return false;
        }

        STRPMApi::Target target{};
        target.kind = STRPMApi::TargetKind::kPlayer;
        target.connectionID = connectionID;
        return SendRaw(target, payload);
    }

    std::optional<RE::FormID> STRPMTransport::ResolveProxy(
        STRPMApi::ConnectionID connectionID) const
    {
        if (!_resolver || !_resolver->resolve || connectionID == 0) {
            return std::nullopt;
        }

        STRPMApi::ProxyFormID formID = 0;
        const auto result =
            _resolver->resolve(connectionID, &formID);

        if (result != STRPMApi::Result::kOk || formID == 0) {
            return std::nullopt;
        }

        return static_cast<RE::FormID>(formID);
    }

    std::optional<STRPMApi::ConnectionID>
        STRPMTransport::ResolveConnection(RE::FormID proxyFormID) const
    {
        if (proxyFormID == 0) {
            return std::nullopt;
        }

        std::scoped_lock lock(_proxyMutex);
        const auto it = _connectionByProxy.find(proxyFormID);
        if (it == _connectionByProxy.end() || it->second == 0) {
            return std::nullopt;
        }
        return it->second;
    }

    void __cdecl STRPMTransport::OnMessage(
        const STRPMApi::Message* message,
        void* userData)
    {
        if (message && userData) {
            static_cast<STRPMTransport*>(userData)->HandleMessage(*message);
        }
    }

    void STRPMTransport::HandleMessage(const STRPMApi::Message& message)
    {
        if (!message.data || message.size == 0 ||
            message.sender.connectionID == 0) {
            return;
        }

        std::string payload(
            static_cast<const char*>(message.data),
            message.size);
        const auto sender = SafeSenderName(message);
        const auto resolved = ResolveProxy(message.sender.connectionID);

        SKSE::log::info(
            "OSTNET STRPM RX connection={} sender=\"{}\" host={} seq={} bytes={} proxy={:08X}",
            message.sender.connectionID,
            sender,
            message.sender.isHost ? 1 : 0,
            message.sequence,
            payload.size(),
            resolved.value_or(0));

        if (auto* tasks = SKSE::GetTaskInterface()) {
            const auto connectionID = message.sender.connectionID;
            tasks->AddTask(
                [connectionID,
                 sender,
                 payload = std::move(payload)]() mutable {
                    if (CoopSessionManager::GetSingleton().HandleIncoming(
                            connectionID,
                            sender,
                            payload)) {
                        return;
                    }

                    ActorResolver::GetSingleton().HandleSTRPMPacket(
                        connectionID,
                        std::move(sender),
                        std::move(payload));
                });
        }
    }

    void __cdecl STRPMTransport::OnProxyMapping(
        const STRPMApi::ProxyMappingEvent* event,
        void* userData)
    {
        if (event && userData) {
            static_cast<STRPMTransport*>(userData)->HandleProxyMapping(*event);
        }
    }

    void STRPMTransport::HandleProxyMapping(
        const STRPMApi::ProxyMappingEvent& event)
    {
        {
            std::scoped_lock lock(_proxyMutex);

            if (event.oldFormID != 0) {
                const auto reverseIt =
                    _connectionByProxy.find(event.oldFormID);
                if (reverseIt != _connectionByProxy.end() &&
                    reverseIt->second == event.connectionID) {
                    _connectionByProxy.erase(reverseIt);
                }
            }

            switch (event.type) {
            case STRPMApi::ProxyMappingEventType::kAdded:
            case STRPMApi::ProxyMappingEventType::kUpdated:
                if (event.connectionID != 0 && event.newFormID != 0) {
                    _proxyByConnection[event.connectionID] = event.newFormID;
                    _connectionByProxy[event.newFormID] = event.connectionID;
                }
                break;

            case STRPMApi::ProxyMappingEventType::kRemoved:
                _proxyByConnection.erase(event.connectionID);
                break;

            case STRPMApi::ProxyMappingEventType::kCleared:
                _proxyByConnection.clear();
                _connectionByProxy.clear();
                break;

            default:
                break;
            }
        }

        SKSE::log::info(
            "OSTNET STRPM PROXY event={} connection={} old={:08X} new={:08X}",
            MappingEventName(event.type),
            event.connectionID,
            event.oldFormID,
            event.newFormID);
    }
}
