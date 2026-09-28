#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"
#include "SKSEPluginVersionCompat.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

struct SKSEInterface;

namespace
{
    constexpr char kDiagnosticChannel[] = "strpm.test";
    constexpr auto kApiRetryDelay = std::chrono::milliseconds(500);
    constexpr auto kConnectionPollDelay = std::chrono::milliseconds(250);
    constexpr auto kConnectionSettleDelay = std::chrono::seconds(1);
    constexpr auto kSendRetryDelay = std::chrono::seconds(2);
    constexpr auto kSendRetryLogInterval = std::chrono::seconds(10);
    constexpr auto kProbeSpacing = std::chrono::seconds(5);
    constexpr auto kHandshakePollDelay = std::chrono::milliseconds(250);
    constexpr auto kProxyPollDelay = std::chrono::milliseconds(250);
    constexpr auto kProxyPendingLogInterval = std::chrono::seconds(15);
    constexpr std::uint32_t kProbeFlags =
        STRPM::kMessageReliable |
        STRPM::kMessageOrdered |
        STRPM::kMessageAllowLoopback;

    using IsSTRSessionConnectedFn = bool(STRPM_CALL*)();

    std::mutex g_logMutex;
    std::jthread g_worker;
    STRPM::ListenerHandle g_listener{};
    std::string g_computerName;
    std::atomic_bool g_peerObserved{ false };
    std::atomic_bool g_peerAckObserved{ false };
    std::atomic<STRPM::ConnectionID> g_peerConnectionID{ 0 };

    void Log(const char* fmt, ...)
    {
        std::scoped_lock lock(g_logMutex);

        FILE* file = nullptr;
        fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingDiagnostic.log", "a");
        if (!file)
            return;

        va_list args;
        va_start(args, fmt);
        std::vfprintf(file, fmt, args);
        va_end(args);
        std::fputc('\n', file);
        std::fclose(file);
    }

    std::string GetDiagnosticComputerName()
    {
        char name[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD length = static_cast<DWORD>(std::size(name));
        if (GetComputerNameA(name, &length) && length > 0)
            return std::string(name, length);
        return "UnknownPC";
    }

    std::string SanitizePayloadForLog(std::string_view value)
    {
        std::string result(value);
        for (auto& c : result)
        {
            if (c == '\r' || c == '\n' || c == '\0')
                c = ' ';
        }
        return result;
    }

    bool IsPeerDiagnosticPayload(std::string_view payload)
    {
        if (!payload.starts_with("STRPM_E2E_V1|"))
            return false;

        const std::string localPcField = "|pc=" + g_computerName + "|";
        return payload.find(localPcField) == std::string_view::npos;
    }

    bool IsSTRSessionConnected()
    {
        const auto bridge = GetModuleHandleW(L"STRPluginMessagingBridge.dll");
        if (!bridge)
            return false;
        const auto raw = GetProcAddress(bridge, "STRPM_IsSTRSessionConnected");
        if (!raw)
            return false;
        return reinterpret_cast<IsSTRSessionConnectedFn>(raw)();
    }

    void ResetSessionState()
    {
        g_peerObserved.store(false);
        g_peerAckObserved.store(false);
        g_peerConnectionID.store(0);
    }

    void STRPM_CALL ReceiveDiagnostic(const STRPM::Message* message, void*)
    {
        if (!message)
            return;

        const auto senderName = message->sender.displayName != nullptr ?
            message->sender.displayName : "<unknown>";
        const std::string_view payloadView(
            message->data != nullptr ? static_cast<const char*>(message->data) : "",
            message->size);
        const auto payload = SanitizePayloadForLog(payloadView);

        Log(
            "E2E RECEIVE channel=%s senderId=%llu senderName='%s' seq=%llu flags=0x%X bytes=%zu payload='%s'",
            message->channel != nullptr ? message->channel : "<null>",
            static_cast<unsigned long long>(message->sender.connectionID),
            senderName,
            static_cast<unsigned long long>(message->sequence),
            message->flags,
            message->size,
            payload.c_str());

        if (!IsSTRSessionConnected() || !IsPeerDiagnosticPayload(payloadView))
            return;

        g_peerConnectionID.store(message->sender.connectionID);
        if (!g_peerObserved.exchange(true))
        {
            Log(
                "E2E PEER OBSERVED senderId=%llu senderName='%s' payload='%s'",
                static_cast<unsigned long long>(message->sender.connectionID),
                senderName,
                payload.c_str());
        }

        if (payloadView.starts_with("STRPM_E2E_V1|ack=1|") &&
            !g_peerAckObserved.exchange(true))
        {
            Log(
                "E2E PEER ACK OBSERVED senderId=%llu senderName='%s'",
                static_cast<unsigned long long>(message->sender.connectionID),
                senderName);
        }
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

    STRPM::Result SendDiagnosticPayload(
        const STRPM::Interface* api,
        const STRPM::Target& target,
        std::string_view payload)
    {
        return api->send(
            kDiagnosticChannel,
            target,
            payload.data(),
            payload.size(),
            kProbeFlags);
    }

    bool ResolvePeerProxy(
        std::stop_token token,
        const STRPM::ProxyResolverInterface* resolver,
        std::uint32_t sessionNumber)
    {
        if (!resolver)
        {
            Log("E2E SESSION %u PROXY RESOLVE unavailable: public ProxyResolver interface not loaded", sessionNumber);
            return false;
        }

        auto nextPendingLog = std::chrono::steady_clock::now() + kProxyPendingLogInterval;
        while (!token.stop_requested())
        {
            if (!IsSTRSessionConnected())
            {
                Log("E2E SESSION %u PROXY RESOLVE aborted: STR disconnected", sessionNumber);
                return false;
            }

            const auto peer = g_peerConnectionID.load();
            if (peer != 0)
            {
                STRPM::ProxyFormID formID = STRPM::kInvalidProxyFormID;
                const auto result = resolver->resolve(peer, &formID);
                if (result == STRPM::Result::kOk && formID != STRPM::kInvalidProxyFormID)
                {
                    Log(
                        "E2E SESSION %u PROXY RESOLVE OK senderId=%llu formId=0x%08X",
                        sessionNumber,
                        static_cast<unsigned long long>(peer),
                        static_cast<unsigned>(formID));
                    return true;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= nextPendingLog)
            {
                Log(
                    "E2E SESSION %u PROXY RESOLVE PENDING senderId=%llu (transport/identity healthy; waiting for STR to publish the remote player proxy)",
                    sessionNumber,
                    static_cast<unsigned long long>(peer));
                nextPendingLog = now + kProxyPendingLogInterval;
            }

            if (!SleepInterruptible(token, kProxyPollDelay))
                return false;
        }
        return false;
    }

    bool WaitForConnection(std::stop_token token)
    {
        while (!token.stop_requested() && !IsSTRSessionConnected())
        {
            if (!SleepInterruptible(token, kConnectionPollDelay))
                return false;
        }
        return !token.stop_requested();
    }

    bool WaitForDisconnect(std::stop_token token)
    {
        while (!token.stop_requested() && IsSTRSessionConnected())
        {
            if (!SleepInterruptible(token, kConnectionPollDelay))
                return false;
        }
        return !token.stop_requested();
    }

    bool RunDiagnosticSession(
        std::stop_token token,
        const STRPM::Interface* api,
        const STRPM::ProxyResolverInterface* proxyResolver,
        std::uint32_t sessionNumber)
    {
        ResetSessionState();

        if (!SleepInterruptible(token, kConnectionSettleDelay))
            return false;
        if (!IsSTRSessionConnected())
        {
            Log("E2E SESSION %u aborted during connection settle", sessionNumber);
            return true;
        }

        Log("E2E SESSION %u START: STR connection observed; diagnostic sends enabled", sessionNumber);

        STRPM::Target target{};
        target.kind = STRPM::TargetKind::kAllPlayers;

        auto lastWaitingLog = std::chrono::steady_clock::time_point{};
        std::uint32_t probeNumber = 1;
        while (!token.stop_requested() && probeNumber <= 2)
        {
            if (!IsSTRSessionConnected())
            {
                Log("E2E SESSION %u aborted: STR disconnected while probes were pending", sessionNumber);
                return true;
            }

            const auto payload =
                std::string("STRPM_E2E_V1|probe=") + std::to_string(probeNumber) +
                "|pc=" + g_computerName +
                "|pid=" + std::to_string(GetCurrentProcessId());

            const auto result = SendDiagnosticPayload(api, target, payload);
            if (result == STRPM::Result::kOk)
            {
                Log(
                    "E2E SESSION %u SEND OK probe=%u target=all flags=0x%X bytes=%zu payload='%s'",
                    sessionNumber,
                    probeNumber,
                    kProbeFlags,
                    payload.size(),
                    payload.c_str());
                ++probeNumber;
                if (probeNumber <= 2 && !SleepInterruptible(token, kProbeSpacing))
                    return false;
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            if (lastWaitingLog.time_since_epoch().count() == 0 ||
                now - lastWaitingLog >= kSendRetryLogInterval)
            {
                Log(
                    "E2E SESSION %u send waiting after connected event: %s",
                    sessionNumber,
                    STRPM::ResultToString(result));
                lastWaitingLog = now;
            }

            if (!SleepInterruptible(token, kSendRetryDelay))
                return false;
        }

        Log("E2E SESSION %u initial probes complete; waiting for a peer probe before sending ACK", sessionNumber);

        bool ackSent = false;
        auto lastAckWaitingLog = std::chrono::steady_clock::time_point{};
        while (!token.stop_requested())
        {
            if (!IsSTRSessionConnected())
            {
                Log("E2E SESSION %u aborted: STR disconnected while waiting for peer/ACK", sessionNumber);
                return true;
            }

            if (g_peerObserved.load() && !ackSent)
            {
                const auto payload =
                    std::string("STRPM_E2E_V1|ack=1|pc=") + g_computerName +
                    "|pid=" + std::to_string(GetCurrentProcessId());
                const auto result = SendDiagnosticPayload(api, target, payload);
                if (result == STRPM::Result::kOk)
                {
                    Log(
                        "E2E SESSION %u ACK SEND OK target=all flags=0x%X bytes=%zu payload='%s'",
                        sessionNumber,
                        kProbeFlags,
                        payload.size(),
                        payload.c_str());
                    ackSent = true;
                }
                else
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (lastAckWaitingLog.time_since_epoch().count() == 0 ||
                        now - lastAckWaitingLog >= kSendRetryLogInterval)
                    {
                        Log(
                            "E2E SESSION %u ACK waiting: %s",
                            sessionNumber,
                            STRPM::ResultToString(result));
                        lastAckWaitingLog = now;
                    }
                }
            }

            if (ackSent && g_peerAckObserved.load())
            {
                Log("E2E SESSION %u BIDIRECTIONAL HANDSHAKE COMPLETE", sessionNumber);
                if (!ResolvePeerProxy(token, proxyResolver, sessionNumber))
                    return IsSTRSessionConnected() ? false : true;

                Log("E2E SESSION %u COMPLETE; waiting for disconnect before starting a new session", sessionNumber);
                WaitForDisconnect(token);
                return !token.stop_requested();
            }

            if (!SleepInterruptible(token, kHandshakePollDelay))
                return false;
        }

        return false;
    }

    void DiagnosticWorker(std::stop_token token)
    {
        Log("diagnostic worker started; waiting for SKSE to load STRPluginMessagingAPI.dll");

        while (!token.stop_requested() &&
               GetModuleHandleW(L"STRPluginMessagingAPI.dll") == nullptr)
        {
            if (!SleepInterruptible(token, kApiRetryDelay))
                return;
        }

        const STRPM::Interface* api = nullptr;
        while (!token.stop_requested())
        {
            api = STRPM::LoadFromModule(L"STRPluginMessagingAPI.dll");
            if (api != nullptr)
                break;
            if (!SleepInterruptible(token, kApiRetryDelay))
                return;
        }
        if (!api)
            return;

        const auto* proxyResolver = STRPM::LoadProxyResolverFromModule(L"STRPluginMessagingAPI.dll");
        Log(
            "public API v%u loaded; ProxyResolver=%s",
            api->version,
            proxyResolver ? "available" : "unavailable");
        g_computerName = GetDiagnosticComputerName();

        const auto registerResult = api->registerChannel(
            kDiagnosticChannel,
            &ReceiveDiagnostic,
            nullptr,
            &g_listener);
        if (registerResult != STRPM::Result::kOk)
        {
            Log(
                "failed to register channel '%s': %s",
                kDiagnosticChannel,
                STRPM::ResultToString(registerResult));
            return;
        }
        Log(
            "registered public callback on channel '%s' for pc='%s'",
            kDiagnosticChannel,
            g_computerName.c_str());

        std::uint32_t sessionNumber = 0;
        while (!token.stop_requested())
        {
            ResetSessionState();
            Log("E2E waiting for authoritative STR OnConnected lifecycle event before starting next session");
            if (!WaitForConnection(token))
                return;

            ++sessionNumber;
            if (!RunDiagnosticSession(token, api, proxyResolver, sessionNumber))
                return;

            if (!IsSTRSessionConnected())
            {
                ResetSessionState();
                Log("E2E SESSION %u reset after disconnect", sessionNumber);
            }
        }
    }
}

extern "C" __declspec(dllexport) STRPMSKSE::PluginVersionData SKSEPlugin_Version =
{
    STRPMSKSE::PluginVersionData::kVersion,
    STRPMSKSE::kPluginVersion_0_9_3,
    "STRPluginMessagingDiagnostic",
    "Caelvanost",
    "",
    0,
    0,
    { STRPMSKSE::kRuntime_1_6_1170, 0 },
    0
};

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface*)
{
    {
        FILE* file = nullptr;
        fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingDiagnostic.log", "w");
        if (file)
        {
            std::fprintf(file, "STRPluginMessagingDiagnostic v0.9.3: SKSEPlugin_Load entered\n");
            std::fclose(file);
        }
    }

    g_worker = std::jthread(&DiagnosticWorker);
    return true;
}