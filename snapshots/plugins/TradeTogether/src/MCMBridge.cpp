#include "PCH.h"
#include "Config.h"
#include "Input.h"
#include "MCMBridge.h"

namespace TradeTogether::MCMBridge
{
    namespace
    {
        constexpr std::string_view kScriptName = "TradeTogetherNative";

        std::uint32_t GetConfiguredKey(std::string_view a_action)
        {
            const auto config = Config::Load();
            if (a_action == "Trade") {
                return config.tradeKey;
            }
            if (a_action == "AddItem") {
                return config.addItemKey;
            }
            if (a_action == "RemoveItem") {
                return config.removeItemKey;
            }
            if (a_action == "GoldAdd") {
                return config.goldAddKey;
            }
            if (a_action == "GoldRemove") {
                return config.goldRemoveKey;
            }
            if (a_action == "Cancel") {
                return config.cancelKey;
            }
            return 0;
        }

        std::int32_t GetKey(RE::StaticFunctionTag*, std::string a_action)
        {
            return static_cast<std::int32_t>(GetConfiguredKey(a_action));
        }

        std::int32_t GetDefaultKey(RE::StaticFunctionTag*, std::string a_action)
        {
            return static_cast<std::int32_t>(Config::DefaultControlKey(a_action));
        }

        bool SetKey(
            RE::StaticFunctionTag*,
            std::string a_action,
            std::int32_t a_scanCode)
        {
            if (a_scanCode < 0 || a_scanCode > 0xFF) {
                spdlog::warn(
                    "MCM rejected invalid key mapping: action={} key={}",
                    a_action,
                    a_scanCode);
                return false;
            }

            if (!Config::WriteControlKey(
                    a_action,
                    static_cast<std::uint32_t>(a_scanCode))) {
                spdlog::error(
                    "MCM failed to persist key mapping: action={} key={}",
                    a_action,
                    a_scanCode);
                return false;
            }

            InputEventSink::GetSingleton()->ReloadConfig();
            spdlog::info(
                "MCM key mapping updated: action={} key=0x{:02X}",
                a_action,
                a_scanCode);
            return true;
        }

        bool ResetKeys(RE::StaticFunctionTag*)
        {
            constexpr std::array<std::string_view, 6> actions{
                "Trade",
                "AddItem",
                "RemoveItem",
                "GoldAdd",
                "GoldRemove",
                "Cancel"
            };

            bool success = true;
            for (const auto action : actions) {
                success = Config::WriteControlKey(
                              action,
                              Config::DefaultControlKey(action)) && success;
            }

            if (success) {
                InputEventSink::GetSingleton()->ReloadConfig();
                spdlog::info("MCM reset all TradeTogether key mappings to defaults");
            } else {
                spdlog::error("MCM failed to reset one or more TradeTogether key mappings");
            }
            return success;
        }
    }

    bool Register(RE::BSScript::IVirtualMachine* a_vm)
    {
        if (!a_vm) {
            return false;
        }

        a_vm->RegisterFunction("GetKey", kScriptName.data(), GetKey);
        a_vm->RegisterFunction("GetDefaultKey", kScriptName.data(), GetDefaultKey);
        a_vm->RegisterFunction("SetKey", kScriptName.data(), SetKey);
        a_vm->RegisterFunction("ResetKeys", kScriptName.data(), ResetKeys);

        spdlog::info("TradeTogether Papyrus MCM bridge registered");
        return true;
    }
}
