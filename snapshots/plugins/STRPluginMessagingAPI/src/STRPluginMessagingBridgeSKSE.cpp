#include "SKSEPluginVersionCompat.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#ifdef small
#undef small
#endif

#include <charconv>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "STRPMChatUiSuppressBootstrap.h"
#include "STRPMProxyResolverBridge.h"
#include "STRPMProxyResolverBootstrapV2.h"
#include "STRPMProxyResolverTrace.h"

struct SKSEInterface;

namespace
{
    constexpr char kIdentityChannel[] = "strpm.identity.v1";
    constexpr auto kIdentityRetryDelay = std::chrono::milliseconds(250);
    constexpr auto kIdentityHeartbeatInterval = std::chrono::seconds(2);

    std::jthread g_identityWorker;
    STRPM::ListenerHandle g_identityListener{};
    bool g_identityListenerRegistered = false;
    std::mutex g_identityObservationMutex;
    std::unordered_map<STRPM::ConnectionID, std::uint32_t> g_identityObservations;

    void LogIdentity(const char* format, ...) noexcept
    {
        FILE* file = nullptr;
        fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
        if (!file)
            return;

        va_list args;
        va_start(args, format);
        std::vfprintf(file, format, args);
        va_end(args);
        std::fputc('\n', file);
        std::fclose(file);
    }

    const STRPM::Interface* QueryMessagingApi() noexcept
    {
        const auto module = GetModuleHandleW(L"STRPluginMessagingAPI.dll");
        if (!module)
            return nullptr;

        const auto raw = GetProcAddress(module, STRPM::kQueryInterfaceExportName);
        if (!raw)
            return nullptr;

        const auto query = reinterpret_cast<STRPM::QueryInterfaceFn>(raw);
        const STRPM::Interface* api = nullptr;
        if (query(STRPM::kInterfaceVersion, &api) != STRPM::Result::kOk ||
            !api || api->version != STRPM::kInterfaceVersion)
        {
            return nullptr;
        }
        return api;
    }

    void STRPM_CALL ReceiveIdentityAnnouncement(const STRPM::Message* message, void*)
    {
        if (!message || message->sender.connectionID == 0 || !message->data || message->size == 0 || message->size > 10)
            return;

        const std::string_view payload(static_cast<const char*>(message->data), message->size);
        std::uint32_t playerId = 0;
        const auto parsed = std::from_chars(payload.data(), payload.data() + payload.size(), playerId);
        if (parsed.ec != std::errc{} || parsed.ptr != payload.data() + payload.size() || playerId == 0)
            return;

        STRPMProxyResolverBridge::detail::ObserveSender(message->sender.connectionID, playerId);

        bool shouldLog = false;
        {
            std::scoped_lock lock(g_identityObservationMutex);
            const auto [it, inserted] = g_identityObservations.try_emplace(
                message->sender.connectionID,
                playerId);
            if (inserted)
            {
                shouldLog = true;
            }
            else if (it->second != playerId)
            {
                it->second = playerId;
                shouldLog = true;
            }
        }

        if (shouldLog)
        {
            LogIdentity(
                "ProxyResolver identity channel observed connection=%llu playerId=%u",
                static_cast<unsigned long long>(message->sender.connectionID),
                static_cast<unsigned>(playerId));
        }
    }

    bool EnsureIdentityListener() noexcept
    {
        if (g_identityListenerRegistered)
            return true;

        const auto* api = QueryMessagingApi();
        if (!api || !api->registerChannel)
            return false;

        const auto result = api->registerChannel(
            kIdentityChannel,
            &ReceiveIdentityAnnouncement,
            nullptr,
            &g_identityListener);
        if (result == STRPM::Result::kOk)
        {
            g_identityListenerRegistered = true;
            LogIdentity("ProxyResolver identity channel listener registered");
            return true;
        }

        return false;
    }

    const STRPM::TransportInterface* QueryTransport() noexcept
    {
        const auto module = GetModuleHandleW(L"STRPluginMessagingBridge.dll");
        if (!module)
            return nullptr;

        const auto raw = GetProcAddress(module, STRPM::kQueryTransportExportName);
        if (!raw)
            return nullptr;

        const auto query = reinterpret_cast<STRPM::QueryTransportInterfaceFn>(raw);
        const STRPM::TransportInterface* transport = nullptr;
        if (query(STRPM::kTransportInterfaceVersion, &transport) != STRPM::Result::kOk ||
            !transport || transport->version != STRPM::kTransportInterfaceVersion)
        {
            return nullptr;
        }
        return transport;
    }

    STRPM::Result SendIdentityAnnouncement() noexcept
    {
        const auto* transport = QueryTransport();
        if (!transport || !transport->send)
            return STRPM::Result::kNotAvailable;

        STRPM::Target target{};
        target.kind = STRPM::TargetKind::kAllPlayers;
        constexpr std::uint32_t flags = STRPM::kMessageReliable | STRPM::kMessageOrdered;

        return transport->send(kIdentityChannel, target, nullptr, 0, flags);
    }

    bool SleepInterruptible(std::stop_token token, std::chrono::milliseconds duration)
    {
        constexpr auto slice = std::chrono::milliseconds(100);
        auto slept = std::chrono::milliseconds(0);
        while (slept < duration && !token.stop_requested())
        {
            const auto remaining = duration - slept;
            const auto current = remaining < slice ? remaining : slice;
            std::this_thread::sleep_for(current);
            slept += current;
        }
        return !token.stop_requested();
    }

    void IdentityWorker(std::stop_token token)
    {
        bool announcedThisSession = false;
        bool loggedWaiting = false;
        bool identityObservationsCleared = false;
        auto nextHeartbeat = std::chrono::steady_clock::time_point{};

        while (!token.stop_requested())
        {
            EnsureIdentityListener();

            if (!STRPMProxyResolverBridge::IsSTRSessionConnected())
            {
                if (!identityObservationsCleared)
                {
                    std::scoped_lock lock(g_identityObservationMutex);
                    g_identityObservations.clear();
                    identityObservationsCleared = true;
                }
                announcedThisSession = false;
                loggedWaiting = false;
                nextHeartbeat = {};
                if (!SleepInterruptible(token, kIdentityRetryDelay))
                    return;
                continue;
            }

            identityObservationsCleared = false;
            const auto now = std::chrono::steady_clock::now();
            if (!announcedThisSession || nextHeartbeat.time_since_epoch().count() == 0 || now >= nextHeartbeat)
            {
                const auto result = SendIdentityAnnouncement();
                if (result == STRPM::Result::kOk)
                {
                    if (!announcedThisSession)
                        LogIdentity("ProxyResolver identity bootstrap announcement sent");
                    announcedThisSession = true;
                    loggedWaiting = false;
                    nextHeartbeat = now + kIdentityHeartbeatInterval;
                }
                else
                {
                    if (!loggedWaiting &&
                        result != STRPM::Result::kNotConnected &&
                        result != STRPM::Result::kNotAvailable)
                    {
                        LogIdentity(
                            "ProxyResolver identity bootstrap waiting: result=%u",
                            static_cast<unsigned>(result));
                        loggedWaiting = true;
                    }
                    nextHeartbeat = now + kIdentityRetryDelay;
                }
            }

            if (!SleepInterruptible(token, kIdentityRetryDelay))
                return;
        }
    }
}

extern "C" __declspec(dllexport) STRPMSKSE::PluginVersionData SKSEPlugin_Version =
{
    STRPMSKSE::PluginVersionData::kVersion,
    STRPMSKSE::kPluginVersion_0_9_3,
    "STRPluginMessagingBridge",
    "Caelvanost",
    "",
    0,
    0,
    { STRPMSKSE::kRuntime_1_6_1170, 0 },
    0
};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface*)
{
    FILE* file = nullptr;
    fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
    if (file != nullptr)
    {
        std::fprintf(file, "STRPluginMessagingBridge v0.9.3: SKSEPlugin_Load entered\n");
        std::fclose(file);
    }

    STRPMChatUiSuppressBootstrap::Start();
    STRPMProxyResolverBootstrapV2::Start();
    STRPMProxyResolverTrace::Start();

    // v0.9.3 keeps the raw OnConsume metadata observer as a fallback, but the
    // primary identity path is now the reserved API channel. The server relay
    // rewrites the zero-byte heartbeat payload with its authenticated PlayerId,
    // so ConnectionID -> PlayerId no longer depends on VEH handler ordering.
    g_identityWorker = std::jthread(&IdentityWorker);

    return true;
}