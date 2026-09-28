#include "PCH.h"
#include "Config.h"

#include <Windows.h>

namespace OStimTogether
{
    namespace
    {
        constexpr wchar_t kIniPath[] =
            L".\\Data\\SKSE\\Plugins\\OStimTogether.ini";

        bool ReadBool(
            const wchar_t* section,
            const wchar_t* key,
            bool fallback)
        {
            return GetPrivateProfileIntW(
                       section,
                       key,
                       fallback ? 1 : 0,
                       kIniPath) != 0;
        }

        std::uint32_t Clamp(
            std::uint32_t value,
            std::uint32_t minValue,
            std::uint32_t maxValue)
        {
            return std::max(
                minValue,
                std::min(value, maxValue));
        }
    }

    Config Config::Load()
    {
        Config cfg{};

        cfg.toggleKey = static_cast<std::uint32_t>(
            GetPrivateProfileIntW(
                L"General",
                L"ToggleKey",
                cfg.toggleKey,
                kIniPath));

        cfg.clearKey = static_cast<std::uint32_t>(
            GetPrivateProfileIntW(
                L"General",
                L"ClearKey",
                cfg.clearKey,
                kIniPath));

        cfg.consentAcceptKey = static_cast<std::uint32_t>(
            GetPrivateProfileIntW(
                L"General",
                L"ConsentAcceptKey",
                cfg.consentAcceptKey,
                kIniPath));

        cfg.consentDeclineKey = static_cast<std::uint32_t>(
            GetPrivateProfileIntW(
                L"General",
                L"ConsentDeclineKey",
                cfg.consentDeclineKey,
                kIniPath));

        cfg.intervalMs = static_cast<std::uint32_t>(
            GetPrivateProfileIntW(
                L"General",
                L"IntervalMs",
                cfg.intervalMs,
                kIniPath));

        cfg.slotMask = static_cast<std::uint32_t>(
            GetPrivateProfileIntW(
                L"Equipment",
                L"SlotMask",
                cfg.slotMask,
                kIniPath));

        cfg.debugNotifications = ReadBool(
            L"General",
            L"DebugNotifications",
            cfg.debugNotifications);

        cfg.intervalMs = Clamp(cfg.intervalMs, 25, 1000);
        return cfg;
    }
}
