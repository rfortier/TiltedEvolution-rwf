#include "PCH.h"
#include "Config.h"

#include <Windows.h>  // after PCH/CommonLib on purpose

namespace MorphSyncTogether
{
    namespace
    {
        constexpr wchar_t kIniPath[] =
            L".\\Data\\SKSE\\Plugins\\MorphSyncTogether.ini";

        std::string ReadString(
            const wchar_t* section,
            const wchar_t* key,
            const wchar_t* fallback)
        {
            wchar_t buffer[512]{};
            GetPrivateProfileStringW(
                section, key, fallback, buffer,
                static_cast<DWORD>(std::size(buffer)), kIniPath);

            if (buffer[0] == L'\0') {
                return {};
            }

            const int needed = WideCharToMultiByte(
                CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 1) {
                return {};
            }

            std::string converted(static_cast<std::size_t>(needed), '\0');
            const int written = WideCharToMultiByte(
                CP_UTF8, 0, buffer, -1, converted.data(), needed, nullptr, nullptr);
            if (written <= 1) {
                return {};
            }

            converted.resize(static_cast<std::size_t>(written - 1));
            return converted;
        }

        bool ReadBool(
            const wchar_t* section,
            const wchar_t* key,
            bool fallback)
        {
            return GetPrivateProfileIntW(
                       section, key, fallback ? 1 : 0, kIniPath) != 0;
        }

        std::uint32_t Clamp(
            std::uint32_t value,
            std::uint32_t minValue,
            std::uint32_t maxValue)
        {
            return std::max(minValue, std::min(value, maxValue));
        }
    }

    Config Config::Load()
    {
        Config cfg{};

        cfg.networkEnabled = !ReadBool(L"Network", L"Disabled", false);
        cfg.autoDiscovery = ReadBool(L"Network", L"AutoDiscovery", true);

        auto localPort = static_cast<std::uint32_t>(GetPrivateProfileIntW(
            L"Network", L"LocalPort", cfg.localPort, kIniPath));
        if (localPort == 0 || localPort > 65535) {
            localPort = 27992;
        }
        cfg.localPort = static_cast<std::uint16_t>(localPort);
        cfg.peerPort = cfg.localPort;

        cfg.discoveryIntervalMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"Network", L"DiscoveryIntervalMs", cfg.discoveryIntervalMs, kIniPath)),
            250, 5000);

        cfg.peerTimeoutMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"Network", L"PeerTimeoutMs", cfg.peerTimeoutMs, kIniPath)),
            3000, 60000);

        if (!cfg.autoDiscovery) {
            cfg.peerHost = ReadString(L"Network", L"PeerHost", L"");
            auto peerPort = static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"Network", L"PeerPort", cfg.peerPort, kIniPath));
            if (peerPort == 0 || peerPort > 65535) {
                peerPort = cfg.localPort;
            }
            cfg.peerPort = static_cast<std::uint16_t>(peerPort);
        }

        cfg.syncIntervalMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"MorphSync", L"SyncIntervalMs", cfg.syncIntervalMs, kIniPath)),
            250, 10000);

        cfg.fullResendMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"MorphSync", L"FullResendMs", cfg.fullResendMs, kIniPath)),
            1000, 60000);

        cfg.remoteReapplyMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"MorphSync", L"RemoteReapplyMs", cfg.remoteReapplyMs, kIniPath)),
            1000, 60000);

        cfg.clearRemoteMorphs = ReadBool(
            L"MorphSync", L"ClearRemoteMorphs", cfg.clearRemoteMorphs);

        cfg.pubicOverlaySyncEnabled = ReadBool(
            L"PubicOverlaySync", L"Enabled", cfg.pubicOverlaySyncEnabled);

        cfg.appearanceProbeEnabled = ReadBool(
            L"AppearanceProbe", L"Enabled", cfg.appearanceProbeEnabled);

        cfg.appearanceProbeIntervalMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"AppearanceProbe", L"IntervalMs", cfg.appearanceProbeIntervalMs, kIniPath)),
            250, 10000);

        cfg.appearanceProbeVerbose = ReadBool(
            L"AppearanceProbe", L"Verbose", cfg.appearanceProbeVerbose);

        cfg.appearancePreserveRemote = ReadBool(
            L"AppearancePreserve", L"Enabled", cfg.appearancePreserveRemote);

        cfg.appearanceRecoveryAttempts = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"AppearancePreserve", L"RecoveryAttempts", cfg.appearanceRecoveryAttempts, kIniPath)),
            1, 10);

        cfg.faceMaterialRebindEnabled = ReadBool(
            L"FaceMaterialRebind", L"Enabled", cfg.faceMaterialRebindEnabled);

        cfg.faceMaterialRebindFollowups = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"FaceMaterialRebind", L"FollowupPasses", cfg.faceMaterialRebindFollowups, kIniPath)),
            1, 10);

        cfg.faceMaterialRebindIntervalMs = Clamp(
            static_cast<std::uint32_t>(GetPrivateProfileIntW(
                L"FaceMaterialRebind", L"FollowupIntervalMs", cfg.faceMaterialRebindIntervalMs, kIniPath)),
            100, 2000);

        return cfg;
    }
}
