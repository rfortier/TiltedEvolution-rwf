#pragma once

#include <cstddef>
#include <cstdint>

#define STRPM_CALL __cdecl

namespace STRPM
{
    inline constexpr std::uint32_t kInterfaceVersion = 2;
    inline constexpr std::uint32_t kProxyResolverVersion = 1;
    inline constexpr char kQueryInterfaceExportName[] = "STR_QueryPluginMessagingInterface";
    inline constexpr char kQueryProxyResolverExportName[] = "STR_QueryPluginMessagingProxyResolver";

    using ConnectionID = std::uint64_t;
    using ProxyFormID = std::uint32_t;

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

    using ReceiveCallback = void(STRPM_CALL*)(const Message*, void*);

    struct Interface
    {
        std::uint32_t version{ kInterfaceVersion };
        Result(STRPM_CALL* registerChannel)(const char*, ReceiveCallback, void*, ListenerHandle*);
        Result(STRPM_CALL* unregisterChannel)(ListenerHandle);
        Result(STRPM_CALL* send)(const char*, Target, const void*, std::size_t, std::uint32_t);
        Result(STRPM_CALL* getLocalConnectionID)(ConnectionID*);
        Result(STRPM_CALL* setLogCallback)(void*, void*);
        Result(STRPM_CALL* setLocalDisplayName)(const char*);
    };

    struct ProxyResolverInterface
    {
        std::uint32_t version{ kProxyResolverVersion };
        Result(STRPM_CALL* resolve)(ConnectionID, ProxyFormID*);
        Result(STRPM_CALL* registerListener)(void*, void*);
        Result(STRPM_CALL* unregisterListener)(void*, void*);
    };

    using QueryInterfaceFn = Result(STRPM_CALL*)(std::uint32_t, const Interface**);
    using QueryProxyResolverFn = Result(STRPM_CALL*)(std::uint32_t, const ProxyResolverInterface**);
}
