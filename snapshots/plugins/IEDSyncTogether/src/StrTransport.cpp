#include "PCH.h"
#include "StrTransport.h"

#include "FormIdentity.h"

#include <Windows.h>

namespace IEDSyncTogether
{
    namespace
    {
        constexpr char kChannel[] = "strpm.iedsynctogether.slots.v1";

        const char* BackendName(STRPM::RuntimeBackend backend)
        {
            switch (backend) {
            case STRPM::RuntimeBackend::kNone:
                return "None";
            case STRPM::RuntimeBackend::kUdp:
                return "UDP";
            case STRPM::RuntimeBackend::kStrBridge:
                return "StrBridge";
            default:
                return "Unknown";
            }
        }

        const char* BackendModeName(STRPM::RuntimeBackendMode mode)
        {
            switch (mode) {
            case STRPM::RuntimeBackendMode::kAuto:
                return "Auto";
            case STRPM::RuntimeBackendMode::kUdp:
                return "UDP";
            case STRPM::RuntimeBackendMode::kStrBridge:
                return "StrBridge";
            default:
                return "Unknown";
            }
        }

        std::string SanitizeField(std::string value)
        {
            for (auto& ch : value) {
                if (ch == '|' || ch == '\r' || ch == '\n') {
                    ch = '_';
                }
            }
            return value.empty() ? "Player" : value;
        }
    }

    StrTransport& StrTransport::GetSingleton()
    {
        static StrTransport instance;
        return instance;
    }

    StrTransport::~StrTransport()
    {
        Stop();
    }

    std::string StrTransport::GetLocalPlayerName() const
    {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            const auto* name = player->GetName();
            if (name && *name) {
                return SanitizeField(name);
            }
        }

        return "Player";
    }

    bool StrTransport::LoadApi()
    {
        auto module = GetModuleHandleW(L"STRPluginMessagingAPI.dll");
        if (!module) {
            module = LoadLibraryW(L"STRPluginMessagingAPI.dll");
        }
        if (!module) {
            module = LoadLibraryW(L"Data\\SKSE\\Plugins\\STRPluginMessagingAPI.dll");
        }
        if (!module) {
            SKSE::log::warn("STRPM transport unavailable: STRPluginMessagingAPI.dll not loaded");
            return false;
        }

        const auto rawQuery =
            GetProcAddress(module, STRPM::kQueryInterfaceExportName);
        if (!rawQuery) {
            SKSE::log::warn("STRPM transport unavailable: query export missing");
            return false;
        }

        const auto query =
            reinterpret_cast<STRPM::QueryInterfaceFn>(rawQuery);
        const STRPM::Interface* api = nullptr;
        const auto result = query(STRPM::kInterfaceVersion, &api);
        if (result != STRPM::Result::kOk || !api || api->version != STRPM::kInterfaceVersion) {
            SKSE::log::warn(
                "STRPM transport unavailable: query failed ({})",
                STRPM::ResultToString(result));
            return false;
        }

        _api = api;
        (void)LoadDiagnostics(module);
        return true;
    }

    bool StrTransport::LoadDiagnostics(void* moduleHandle)
    {
        const auto module = static_cast<HMODULE>(moduleHandle);
        _diagnostics = nullptr;
        const auto rawQuery =
            GetProcAddress(module, STRPM::kQueryDiagnosticsExportName);
        if (!rawQuery) {
            SKSE::log::warn("STRPM diagnostics unavailable: query export missing");
            return false;
        }

        const auto query =
            reinterpret_cast<STRPM::QueryDiagnosticsFn>(rawQuery);
        const STRPM::DiagnosticsInterface* diagnostics = nullptr;
        const auto result = query(STRPM::kDiagnosticsVersion, &diagnostics);
        if (result != STRPM::Result::kOk ||
            !diagnostics ||
            diagnostics->version != STRPM::kDiagnosticsVersion ||
            !diagnostics->getRuntimeStatus) {
            SKSE::log::warn(
                "STRPM diagnostics unavailable: query failed ({})",
                STRPM::ResultToString(result));
            return false;
        }

        _diagnostics = diagnostics;
        return true;
    }

    std::optional<STRPM::RuntimeStatus> StrTransport::ReadRuntimeStatus() const
    {
        if (!_diagnostics || !_diagnostics->getRuntimeStatus) {
            return std::nullopt;
        }

        STRPM::RuntimeStatus status{};
        const auto result = _diagnostics->getRuntimeStatus(&status);
        if (result != STRPM::Result::kOk || status.version != STRPM::kDiagnosticsVersion) {
            SKSE::log::warn(
                "STRPM diagnostics status unavailable: {} version={}",
                STRPM::ResultToString(result),
                status.version);
            return std::nullopt;
        }

        return status;
    }

    bool StrTransport::ValidateRuntimeBackend() const
    {
        const auto status = ReadRuntimeStatus();
        if (!status) {
            if (_config.requireStrBridge) {
                SKSE::log::warn(
                    "STRPM transport rejected: RequireStrBridge=1 but diagnostics are unavailable");
                return false;
            }
            return true;
        }

        SKSE::log::info(
            "STRPM runtime status backend={} configured={} bridgeAvailable={} bridgeActive={} peers={} configuredPeers={}",
            BackendName(status->activeBackend),
            BackendModeName(status->configuredBackendMode),
            status->strBridgeAvailable,
            status->strBridgeActive,
            status->knownPeerCount,
            status->configuredPeerCount);

        if (!_config.requireStrBridge) {
            return true;
        }

        if (status->activeBackend != STRPM::RuntimeBackend::kStrBridge ||
            status->strBridgeActive == 0) {
            SKSE::log::warn(
                "STRPM transport rejected: RequireStrBridge=1 but activeBackend={} bridgeAvailable={} bridgeActive={}",
                BackendName(status->activeBackend),
                status->strBridgeAvailable,
                status->strBridgeActive);
            return false;
        }

        return true;
    }

    std::optional<std::string> StrTransport::ReadField(
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

    bool StrTransport::Start(const Config& config, PacketHandler handler)
    {
        if (_running.load()) {
            return true;
        }
        if (!config.networkEnabled || !handler) {
            return false;
        }

        _config = config;
        _handler = std::move(handler);
        if (!LoadApi()) {
            _handler = {};
            return false;
        }
        if (!ValidateRuntimeBackend()) {
            _api = nullptr;
            _diagnostics = nullptr;
            _handler = {};
            return false;
        }

        if (_api->setLocalDisplayName) {
            const auto name = GetLocalPlayerName();
            const auto result = _api->setLocalDisplayName(name.c_str());
            if (result != STRPM::Result::kOk) {
                SKSE::log::warn(
                    "STRPM setLocalDisplayName failed: {}",
                    STRPM::ResultToString(result));
            }
        }

        if (_api->getLocalConnectionID) {
            const auto result = _api->getLocalConnectionID(&_localConnectionID);
            if (result != STRPM::Result::kOk) {
                SKSE::log::warn(
                    "STRPM getLocalConnectionID failed: {}",
                    STRPM::ResultToString(result));
            }
        }
        _localInstanceID = _localConnectionID != 0 ?
            std::to_string(_localConnectionID) :
            fmt::format("STRPM-{:08X}-{:016X}", GetCurrentProcessId(), GetTickCount64());

        const auto result = _api->registerChannel(
            kChannel,
            &StrTransport::OnMessage,
            this,
            &_listener);
        if (result != STRPM::Result::kOk) {
            SKSE::log::warn(
                "STRPM registerChannel failed: {}",
                STRPM::ResultToString(result));
            _api = nullptr;
            _handler = {};
            return false;
        }

        _running.store(true);
        SKSE::log::info(
            "STRPM transport started channel={} player=\"{}\" connectionID={} instance={}",
            kChannel,
            GetLocalPlayerName(),
            _localConnectionID,
            _localInstanceID);
        return true;
    }

    void StrTransport::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_api && _api->unregisterChannel && _listener.value != 0) {
            _api->unregisterChannel(_listener);
        }
        _listener = {};
        _localConnectionID = 0;
        _localInstanceID.clear();
        _diagnostics = nullptr;
        _api = nullptr;
        _handler = {};
        SKSE::log::info("STRPM transport stopped");
    }

    std::string StrTransport::BuildPacket(std::string_view payload) const
    {
        return fmt::format(
            "IEDST|v1|id={}|from={}|relay=0|{}",
            _localInstanceID,
            HexEncode(GetLocalPlayerName()),
            payload);
    }

    void StrTransport::Send(std::string_view payload)
    {
        if (!_running.load() || !_api || !_api->send || payload.empty()) {
            return;
        }

        const auto packet = BuildPacket(payload);
        if (packet.size() > STRPM::kMaxPayloadBytes) {
            SKSE::log::warn(
                "STRPM TX dropped: payload too large ({} bytes)",
                packet.size());
            return;
        }

        const STRPM::Target target{
            STRPM::TargetKind::kAllPlayers,
            0,
            nullptr
        };

        std::scoped_lock lock(_sendMutex);
        const auto result = _api->send(
            kChannel,
            target,
            packet.data(),
            packet.size(),
            STRPM::kMessageReliable | STRPM::kMessageOrdered);
        if (result != STRPM::Result::kOk) {
            SKSE::log::warn(
                "STRPM send failed: {}",
                STRPM::ResultToString(result));
            if (result == STRPM::Result::kNotConnected) {
                (void)ValidateRuntimeBackend();
            }
        }
    }

    void STRPM_CALL StrTransport::OnMessage(
        const STRPM::Message* message,
        void* userData)
    {
        auto* self = static_cast<StrTransport*>(userData);
        if (!self ||
            !self->_running.load() ||
            !message ||
            !message->data ||
            message->size == 0 ||
            message->size > STRPM::kMaxPayloadBytes) {
            return;
        }

        const std::string packet{
            static_cast<const char*>(message->data),
            message->size
        };

        const auto id = ReadField(packet, "id");
        if (id && !self->_localInstanceID.empty() && *id == self->_localInstanceID) {
            return;
        }
        if (message->sender.connectionID != 0 &&
            self->_localConnectionID != 0 &&
            message->sender.connectionID == self->_localConnectionID) {
            return;
        }

        if (self->_handler) {
            self->_handler(packet);
        }
    }
}
