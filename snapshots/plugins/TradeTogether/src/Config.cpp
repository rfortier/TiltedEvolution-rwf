#include "PCH.h"
#include "Config.h"

namespace TradeTogether
{
    namespace
    {
        constexpr wchar_t kIniPath[] =
            L".\\Data\\SKSE\\Plugins\\TradeTogether.ini";

        std::uint32_t ReadUInt(
            const wchar_t* a_section,
            const wchar_t* a_key,
            std::uint32_t a_fallback)
        {
            return static_cast<std::uint32_t>(
                GetPrivateProfileIntW(
                    a_section,
                    a_key,
                    static_cast<int>(a_fallback),
                    kIniPath));
        }

        bool ReadBool(
            const wchar_t* a_section,
            const wchar_t* a_key,
            bool a_fallback)
        {
            return ReadUInt(
                       a_section,
                       a_key,
                       a_fallback ? 1U : 0U) != 0;
        }

        std::string ReadString(
            const wchar_t* a_section,
            const wchar_t* a_key,
            const wchar_t* a_fallback)
        {
            std::array<wchar_t, 256> buffer{};
            GetPrivateProfileStringW(
                a_section,
                a_key,
                a_fallback,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                kIniPath);

            const auto required = WideCharToMultiByte(
                CP_UTF8,
                0,
                buffer.data(),
                -1,
                nullptr,
                0,
                nullptr,
                nullptr);
            if (required <= 1) {
                return {};
            }

            std::string value(static_cast<std::size_t>(required), '\0');
            const auto written = WideCharToMultiByte(
                CP_UTF8,
                0,
                buffer.data(),
                -1,
                value.data(),
                required,
                nullptr,
                nullptr);
            if (written <= 1) {
                return {};
            }

            value.resize(static_cast<std::size_t>(written - 1));
            return value;
        }

        std::uint16_t ReadPort(
            const wchar_t* a_key,
            std::uint16_t a_fallback)
        {
            const auto value = ReadUInt(
                L"Network",
                a_key,
                a_fallback);
            return value > 0 && value <= 65535 ?
                static_cast<std::uint16_t>(value) : a_fallback;
        }

        std::uint32_t ReadScanCode(
            const wchar_t* a_key,
            std::uint32_t a_fallback)
        {
            const auto value = ReadUInt(L"Controls", a_key, a_fallback);
            return value <= 0xFF ? value : a_fallback;
        }

        const wchar_t* ControlIniKey(std::string_view a_action)
        {
            if (a_action == "Trade") {
                return L"TradeKey";
            }
            if (a_action == "AddItem") {
                return L"AddItemKey";
            }
            if (a_action == "RemoveItem") {
                return L"RemoveItemKey";
            }
            if (a_action == "GoldAdd") {
                return L"GoldAddKey";
            }
            if (a_action == "GoldRemove") {
                return L"GoldRemoveKey";
            }
            if (a_action == "Cancel") {
                return L"CancelKey";
            }
            return nullptr;
        }
    }

    Config Config::Load()
    {
        Config config{};

        config.networkEnabled =
            !ReadBool(L"Network", L"Disabled", false);
        config.autoDiscovery =
            ReadBool(L"Network", L"AutoDiscovery", config.autoDiscovery);
        config.localPort =
            ReadPort(L"LocalPort", config.localPort);
        config.peerPort =
            ReadPort(L"PeerPort", config.localPort);
        config.peerHost =
            ReadString(L"Network", L"PeerHost", L"");
        config.discoveryIntervalMs = std::clamp(
            ReadUInt(
                L"Network",
                L"DiscoveryIntervalMs",
                config.discoveryIntervalMs),
            250U,
            30000U);
        config.peerTimeoutMs = std::clamp(
            ReadUInt(
                L"Network",
                L"PeerTimeoutMs",
                config.peerTimeoutMs),
            2000U,
            120000U);
        config.requestTimeoutMs = std::clamp(
            ReadUInt(
                L"Trade",
                L"RequestTimeoutMs",
                config.requestTimeoutMs),
            5000U,
            120000U);
        config.sessionTimeoutMs = std::clamp(
            ReadUInt(
                L"Trade",
                L"SessionTimeoutMs",
                config.sessionTimeoutMs),
            60000U,
            1800000U);

        config.tradeKey = ReadScanCode(L"TradeKey", config.tradeKey);
        config.addItemKey = ReadScanCode(L"AddItemKey", config.addItemKey);
        config.removeItemKey = ReadScanCode(L"RemoveItemKey", config.removeItemKey);
        config.goldAddKey = ReadScanCode(L"GoldAddKey", config.goldAddKey);
        config.goldRemoveKey = ReadScanCode(L"GoldRemoveKey", config.goldRemoveKey);
        config.cancelKey = ReadScanCode(L"CancelKey", config.cancelKey);

        return config;
    }

    bool Config::WriteControlKey(
        std::string_view a_action,
        std::uint32_t a_scanCode)
    {
        const auto* key = ControlIniKey(a_action);
        if (!key || a_scanCode > 0xFF) {
            return false;
        }

        const auto value = std::to_wstring(a_scanCode);
        return WritePrivateProfileStringW(
                   L"Controls",
                   key,
                   value.c_str(),
                   kIniPath) != FALSE;
    }

    std::uint32_t Config::DefaultControlKey(std::string_view a_action)
    {
        const Config defaults{};
        if (a_action == "Trade") {
            return defaults.tradeKey;
        }
        if (a_action == "AddItem") {
            return defaults.addItemKey;
        }
        if (a_action == "RemoveItem") {
            return defaults.removeItemKey;
        }
        if (a_action == "GoldAdd") {
            return defaults.goldAddKey;
        }
        if (a_action == "GoldRemove") {
            return defaults.goldRemoveKey;
        }
        if (a_action == "Cancel") {
            return defaults.cancelKey;
        }
        return 0;
    }
}
