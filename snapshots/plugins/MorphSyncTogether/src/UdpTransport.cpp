#include "PCH.h"
#include "UdpTransport.h"
#include "MorphSyncService.h"

#include <Windows.h>

namespace MorphSyncTogether
{
    namespace
    {
        constexpr char kChannel[] = "morphsync.together.v1";
        constexpr std::string_view kGameplayPrefix = "MSTUDP|v1|";
    }

    UdpTransport& UdpTransport::GetSingleton()
    {
        static UdpTransport instance;
        return instance;
    }

    UdpTransport::~UdpTransport()
    {
        Stop();
    }

    std::string UdpTransport::SanitizeField(std::string value)
    {
        for (auto& c : value) {
            if (c == '|' || c == '\r' || c == '\n') {
                c = '_';
            }
        }
        if (value.empty()) {
            value = "Player";
        }
        return value;
    }

    const char* UdpTransport::ResultName(STRPM::Result result) noexcept
    {
        switch (result) {
        case STRPM::Result::kOk: return "ok";
        case STRPM::Result::kNotAvailable: return "not-available";
        case STRPM::Result::kUnsupportedVersion: return "unsupported-version";
        case STRPM::Result::kInvalidArgument: return "invalid-argument";
        case STRPM::Result::kNotConnected: return "not-connected";
        case STRPM::Result::kChannelAlreadyRegistered: return "channel-already-registered";
        case STRPM::Result::kChannelNotRegistered: return "channel-not-registered";
        case STRPM::Result::kPayloadTooLarge: return "payload-too-large";
        case STRPM::Result::kRateLimited: return "rate-limited";
        case STRPM::Result::kTransportError: return "transport-error";
        case STRPM::Result::kTargetNotFound: return "target-not-found";
        default: return "unknown";
        }
    }

    std::string UdpTransport::GetLocalClientName() const
    {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (const auto* name = player->GetName(); name && *name) {
                return SanitizeField(name);
            }
        }
        return "MorphSyncTogether";
    }

    bool UdpTransport::Start()
    {
        if (_running.load()) {
            return true;
        }

        _module = GetModuleHandleW(L"STRPluginMessagingAPI.dll");
        if (!_module) {
            SKSE::log::critical(
                "MST STRPM unavailable: STRPluginMessagingAPI.dll is not loaded");
            return false;
        }

        const auto queryApi = reinterpret_cast<STRPM::QueryInterfaceFn>(
            GetProcAddress(_module, STRPM::kQueryInterfaceExportName));
        const auto queryResolver = reinterpret_cast<STRPM::QueryProxyResolverFn>(
            GetProcAddress(_module, STRPM::kQueryProxyResolverExportName));

        if (!queryApi || !queryResolver) {
            SKSE::log::critical(
                "MST STRPM unavailable: required API exports are missing messaging={} resolver={}",
                queryApi ? 1 : 0,
                queryResolver ? 1 : 0);
            return false;
        }

        auto result = queryApi(STRPM::kInterfaceVersion, &_api);
        if (result != STRPM::Result::kOk || !_api) {
            SKSE::log::critical(
                "MST STRPM messaging query failed result={}",
                ResultName(result));
            _api = nullptr;
            return false;
        }

        result = queryResolver(STRPM::kProxyResolverVersion, &_resolver);
        if (result != STRPM::Result::kOk || !_resolver) {
            SKSE::log::critical(
                "MST STRPM ProxyResolver query failed result={}",
                ResultName(result));
            _api = nullptr;
            _resolver = nullptr;
            return false;
        }

        _localName = GetLocalClientName();
        if (_api->setLocalDisplayName) {
            const auto nameResult = _api->setLocalDisplayName(_localName.c_str());
            if (nameResult != STRPM::Result::kOk) {
                SKSE::log::warn(
                    "MST STRPM setLocalDisplayName failed result={}",
                    ResultName(nameResult));
            }
        }

        result = _api->registerChannel(
            kChannel,
            &UdpTransport::OnMessage,
            this,
            &_listener);
        if (result != STRPM::Result::kOk) {
            SKSE::log::critical(
                "MST STRPM registerChannel failed channel={} result={}",
                kChannel,
                ResultName(result));
            _api = nullptr;
            _resolver = nullptr;
            _listener = {};
            return false;
        }

        _running.store(true);
        SKSE::log::info(
            "MST STRPM transport READY channel={} client=\"{}\" messaging=v{} resolver=v{}",
            kChannel,
            _localName,
            STRPM::kInterfaceVersion,
            STRPM::kProxyResolverVersion);
        return true;
    }

    void UdpTransport::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_api && _listener.value != 0) {
            const auto result = _api->unregisterChannel(_listener);
            if (result != STRPM::Result::kOk) {
                SKSE::log::warn(
                    "MST STRPM unregisterChannel failed result={}",
                    ResultName(result));
            }
        }

        {
            std::scoped_lock lock(_senderMutex);
            _senderConnections.clear();
        }

        _listener = {};
        _resolver = nullptr;
        _api = nullptr;
        _module = nullptr;
        SKSE::log::info("MST STRPM transport stopped");
    }

    void UdpTransport::Send(std::string_view payload)
    {
        if (!_running.load() || !_api || payload.empty()) {
            return;
        }

        const STRPM::Target target{
            STRPM::TargetKind::kAllPlayers,
            0,
            nullptr
        };

        const auto result = _api->send(
            kChannel,
            target,
            payload.data(),
            payload.size(),
            STRPM::kMessageReliable | STRPM::kMessageOrdered);

        if (result != STRPM::Result::kOk) {
            SKSE::log::warn(
                "MST STRPM TX failed bytes={} result={}",
                payload.size(),
                ResultName(result));
        }
    }

    void STRPM_CALL UdpTransport::OnMessage(
        const STRPM::Message* message,
        void* userData)
    {
        auto* self = static_cast<UdpTransport*>(userData);
        if (!self || !message || !message->data || message->size == 0) {
            return;
        }
        self->HandleMessage(*message);
    }

    void UdpTransport::HandleMessage(const STRPM::Message& message)
    {
        if (!_running.load()) {
            return;
        }

        std::string sender = message.sender.displayName && *message.sender.displayName ?
            message.sender.displayName :
            fmt::format("connection-{}", message.sender.connectionID);
        sender = SanitizeField(std::move(sender));

        {
            std::scoped_lock lock(_senderMutex);
            _senderConnections[sender] = message.sender.connectionID;
        }

        const std::string_view payload{
            static_cast<const char*>(message.data),
            message.size
        };

        auto packet = fmt::format(
            "{}from={}|{}",
            kGameplayPrefix,
            sender,
            payload);

        SKSE::log::trace(
            "MST STRPM RX sender=\"{}\" connection={} bytes={} sequence={}",
            sender,
            message.sender.connectionID,
            message.size,
            message.sequence);

        QueueMessage(std::move(packet));
    }

    void UdpTransport::QueueMessage(std::string packet)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            SKSE::log::warn("MST STRPM RX dropped: SKSE task interface unavailable");
            return;
        }

        tasks->AddTask([packet = std::move(packet)]() mutable {
            MorphSyncService::GetSingleton().HandleUdpPacket(std::move(packet));
        });
    }

    RE::Actor* UdpTransport::ResolveProxyBySender(std::string_view sender) const
    {
        if (!_running.load() || !_resolver || sender.empty()) {
            return nullptr;
        }

        STRPM::ConnectionID connectionID{};
        {
            std::scoped_lock lock(_senderMutex);
            const auto it = _senderConnections.find(std::string(sender));
            if (it == _senderConnections.end()) {
                return nullptr;
            }
            connectionID = it->second;
        }

        STRPM::ProxyFormID formID{};
        const auto result = _resolver->resolve(connectionID, &formID);
        if (result != STRPM::Result::kOk || formID == 0) {
            return nullptr;
        }

        return RE::TESForm::LookupByID<RE::Actor>(formID);
    }
}
