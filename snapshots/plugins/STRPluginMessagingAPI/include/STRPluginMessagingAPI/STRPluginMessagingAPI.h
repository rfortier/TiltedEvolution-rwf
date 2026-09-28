#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define STRPM_CALL __cdecl
#define STRPM_EXPORT extern "C" __declspec(dllexport)
#else
#define STRPM_CALL
#define STRPM_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace STRPM
{
    inline constexpr std::uint32_t kInterfaceVersion = 2;
    inline constexpr std::uint32_t kDiagnosticsVersion = 2;
    inline constexpr std::uint32_t kTransportInterfaceVersion = 1;
    inline constexpr std::uint32_t kProxyResolverVersion = 1;
    inline constexpr std::uint32_t kMaxChannelLength = 96;
    inline constexpr std::uint32_t kMaxPayloadBytes = 24 * 1024;
    inline constexpr char kQueryInterfaceExportName[] =
        "STR_QueryPluginMessagingInterface";
    inline constexpr char kQueryDiagnosticsExportName[] =
        "STR_QueryPluginMessagingDiagnostics";
    inline constexpr char kQueryTransportExportName[] =
        "STRPM_QueryTransportInterface";
    inline constexpr char kQueryProxyResolverExportName[] =
        "STR_QueryPluginMessagingProxyResolver";

    using ConnectionID = std::uint64_t;
    using ProxyFormID = std::uint32_t;
    inline constexpr ProxyFormID kInvalidProxyFormID = 0;

    enum class Result : std::uint32_t
    {
        kOk = 0,
        kNotAvailable = 1,
        kUnsupportedVersion = 2,
        kInvalidArgument = 3,
        kNotConnected = 4,
        kChannelAlreadyRegistered = 5,
        kChannelNotRegistered = 6,
        kPayloadTooLarge = 7,
        kRateLimited = 8,
        kTransportError = 9,
        kTargetNotFound = 10
    };

    enum MessageFlags : std::uint32_t
    {
        kMessageNone = 0,
        kMessageReliable = 1u << 0,
        kMessageOrdered = 1u << 1,
        kMessageAllowLoopback = 1u << 2
    };

    enum class TargetKind : std::uint32_t
    {
        kServer = 1,
        kHost = 2,
        kPlayer = 3,
        kAllPlayers = 4
    };

    enum class RuntimeBackend : std::uint32_t
    {
        kNone = 0,
        kUdp = 1,
        kStrBridge = 2
    };

    enum class RuntimeBackendMode : std::uint32_t
    {
        kAuto = 0,
        kUdp = 1,
        kStrBridge = 2
    };

    enum class ProxyMappingEventType : std::uint32_t
    {
        kAdded = 1,
        kUpdated = 2,
        kRemoved = 3,
        kCleared = 4
    };

    struct Target
    {
        TargetKind kind{ TargetKind::kAllPlayers };
        ConnectionID connectionID{ 0 };
        const char* displayName{ nullptr };
    };

    struct Sender
    {
        ConnectionID connectionID{ 0 };
        const char* displayName{ nullptr };
        bool isHost{ false };
    };

    struct Message
    {
        const char* channel{ nullptr };
        const void* data{ nullptr };
        std::size_t size{ 0 };
        Sender sender{};
        std::uint32_t flags{ kMessageNone };
        std::uint64_t sequence{ 0 };
    };

    struct ListenerHandle
    {
        std::uint64_t value{ 0 };
    };

    struct ProxyMappingEvent
    {
        ProxyMappingEventType type{ ProxyMappingEventType::kAdded };
        ConnectionID connectionID{ 0 };
        ProxyFormID oldFormID{ kInvalidProxyFormID };
        ProxyFormID newFormID{ kInvalidProxyFormID };
    };

    using ReceiveCallback = void(STRPM_CALL*)(
        const Message* message,
        void* userData);

    using LogCallback = void(STRPM_CALL*)(
        const char* message,
        void* userData);

    using ProxyMappingCallback = void(STRPM_CALL*)(
        const ProxyMappingEvent* event,
        void* userData);

    struct Interface
    {
        std::uint32_t version{ kInterfaceVersion };

        Result(STRPM_CALL* registerChannel)(
            const char* channel,
            ReceiveCallback callback,
            void* userData,
            ListenerHandle* outHandle);

        Result(STRPM_CALL* unregisterChannel)(
            ListenerHandle handle);

        Result(STRPM_CALL* send)(
            const char* channel,
            Target target,
            const void* data,
            std::size_t size,
            std::uint32_t flags);

        Result(STRPM_CALL* getLocalConnectionID)(
            ConnectionID* outConnectionID);

        Result(STRPM_CALL* setLogCallback)(
            LogCallback callback,
            void* userData);

        Result(STRPM_CALL* setLocalDisplayName)(
            const char* displayName);
    };

    struct RuntimeStatus
    {
        std::uint32_t version{ kDiagnosticsVersion };
        std::uint32_t knownPeerCount{ 0 };
        std::uint32_t configuredPeerCount{ 0 };
        std::uint32_t autoDiscovery{ 0 };
        std::uint32_t relayMode{ 0 };
        std::uint32_t requireKnownPeer{ 0 };
        std::uint16_t localPort{ 0 };
        std::uint16_t reserved{ 0 };
        RuntimeBackend activeBackend{ RuntimeBackend::kNone };
        RuntimeBackendMode configuredBackendMode{ RuntimeBackendMode::kAuto };
        std::uint32_t strBridgeAvailable{ 0 };
        std::uint32_t strBridgeActive{ 0 };
    };

    struct DiagnosticsInterface
    {
        std::uint32_t version{ kDiagnosticsVersion };

        Result(STRPM_CALL* getRuntimeStatus)(
            RuntimeStatus* outStatus);
    };

    struct TransportInterface
    {
        std::uint32_t version{ kTransportInterfaceVersion };

        Result(STRPM_CALL* start)(
            ReceiveCallback callback,
            void* userData);

        Result(STRPM_CALL* stop)();

        Result(STRPM_CALL* send)(
            const char* channel,
            Target target,
            const void* data,
            std::size_t size,
            std::uint32_t flags);

        Result(STRPM_CALL* getLocalConnectionID)(
            ConnectionID* outConnectionID);

        Result(STRPM_CALL* setLocalDisplayName)(
            const char* displayName);
    };

    struct ProxyResolverInterface
    {
        std::uint32_t version{ kProxyResolverVersion };

        Result(STRPM_CALL* resolve)(
            ConnectionID connectionID,
            ProxyFormID* outFormID);

        Result(STRPM_CALL* registerListener)(
            ProxyMappingCallback callback,
            void* userData);

        Result(STRPM_CALL* unregisterListener)(
            ProxyMappingCallback callback,
            void* userData);
    };

    using QueryInterfaceFn = Result(STRPM_CALL*)(
        std::uint32_t requestedVersion,
        const Interface** outInterface);

    using QueryDiagnosticsFn = Result(STRPM_CALL*)(
        std::uint32_t requestedVersion,
        const DiagnosticsInterface** outInterface);

    using QueryTransportInterfaceFn = Result(STRPM_CALL*)(
        std::uint32_t requestedVersion,
        const TransportInterface** outInterface);

    using QueryProxyResolverFn = Result(STRPM_CALL*)(
        std::uint32_t requestedVersion,
        const ProxyResolverInterface** outInterface);

    [[nodiscard]] const Interface* LoadFromModule(
        const wchar_t* moduleName = L"STRPluginMessagingAPI.dll") noexcept;

    [[nodiscard]] const DiagnosticsInterface* LoadDiagnosticsFromModule(
        const wchar_t* moduleName = L"STRPluginMessagingAPI.dll") noexcept;

    [[nodiscard]] const TransportInterface* LoadTransportFromModule(
        const wchar_t* moduleName) noexcept;

    [[nodiscard]] const ProxyResolverInterface* LoadProxyResolverFromModule(
        const wchar_t* moduleName = L"STRPluginMessagingAPI.dll") noexcept;

    [[nodiscard]] const char* ResultToString(Result result) noexcept;
}
