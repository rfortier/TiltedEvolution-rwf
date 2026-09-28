#include "PCH.h"
#include "Config.h"
#include "Input.h"
#include "Trade.h"

namespace TradeTogether
{
    InputEventSink* InputEventSink::GetSingleton()
    {
        static InputEventSink singleton;
        return std::addressof(singleton);
    }

    void InputEventSink::ReloadConfig()
    {
        const auto config = Config::Load();
        _tradeKey.store(config.tradeKey, std::memory_order_relaxed);
        _addItemKey.store(config.addItemKey, std::memory_order_relaxed);
        _removeItemKey.store(config.removeItemKey, std::memory_order_relaxed);
        _goldAddKey.store(config.goldAddKey, std::memory_order_relaxed);
        _goldRemoveKey.store(config.goldRemoveKey, std::memory_order_relaxed);
        _cancelKey.store(config.cancelKey, std::memory_order_relaxed);

        spdlog::info(
            "TradeTogether controls loaded: trade=0x{:02X} add=0x{:02X} remove=0x{:02X} goldAdd=0x{:02X} goldRemove=0x{:02X} cancel=0x{:02X}",
            config.tradeKey,
            config.addItemKey,
            config.removeItemKey,
            config.goldAddKey,
            config.goldRemoveKey,
            config.cancelKey);
    }

    void InputEventSink::Register()
    {
        auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (!inputManager) {
            spdlog::error("Could not register input sink: BSInputDeviceManager is null");
            return;
        }

        ReloadConfig();
        inputManager->AddEventSink(GetSingleton());
        spdlog::info(
            "Input event sink registered with configurable key bindings; MessageBoxMenu input isolated");
    }

    RE::BSEventNotifyControl InputEventSink::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        Trade::Update();

        static bool addWasDown = false;
        static bool subtractWasDown = false;
        static bool messageBoxWasOpen = false;

        const auto tradeKey = _tradeKey.load(std::memory_order_relaxed);
        const auto addItemKey = _addItemKey.load(std::memory_order_relaxed);
        const auto removeItemKey = _removeItemKey.load(std::memory_order_relaxed);
        const auto goldAddKey = _goldAddKey.load(std::memory_order_relaxed);
        const auto goldRemoveKey = _goldRemoveKey.load(std::memory_order_relaxed);
        const auto cancelKey = _cancelKey.load(std::memory_order_relaxed);

        // Numpad +/- are a special case: SkyUI/InventoryMenu does not always
        // expose them through ButtonEvent. Preserve the proven Win32 edge
        // detection path whenever either gold action is actually bound to the
        // traditional numpad key. Any other MCM-selected key uses ButtonEvent.
        const bool pollNumpadAdd = goldAddKey == 0x4E;
        const bool pollNumpadSubtract = goldRemoveKey == 0x4A;
        const bool addDown = pollNumpadAdd &&
            (GetAsyncKeyState(VK_ADD) & 0x8000) != 0;
        const bool subtractDown = pollNumpadSubtract &&
            (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0;

        // Skyrim Souls RE deliberately lets MessageBoxMenu run while the game is
        // unpaused. Never let a key intended for a MessageBox trigger a trade
        // action behind it.
        auto* ui = RE::UI::GetSingleton();
        const bool messageBoxOpen =
            ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME);
        if (messageBoxOpen) {
            if (!messageBoxWasOpen) {
                spdlog::debug(
                    "MessageBoxMenu opened; TradeTogether hotkeys suspended until it closes");
            }
            messageBoxWasOpen = true;
            addWasDown = addDown;
            subtractWasDown = subtractDown;
            return RE::BSEventNotifyControl::kContinue;
        }
        if (messageBoxWasOpen) {
            spdlog::debug("MessageBoxMenu closed; TradeTogether hotkeys resumed");
            messageBoxWasOpen = false;
        }

        if ((addDown && !addWasDown) ||
            (subtractDown && !subtractWasDown)) {
            const bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const std::int32_t step = control ? 100 : (shift ? 10 : 1);
            const std::int32_t delta = addDown && !addWasDown ? step : -step;

            spdlog::info(
                "Trade gold key pressed: source=Win32 numpad delta={}",
                delta);
            Trade::AdjustGold(delta);
        }

        addWasDown = addDown;
        subtractWasDown = subtractDown;

        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            if (event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }

            auto* button = event->AsButtonEvent();
            if (!button || !button->IsDown()) {
                continue;
            }

            const auto scanCode = button->GetIDCode();

            if (scanCode == tradeKey) {
                spdlog::debug(
                    "Trade request/validate key pressed: DIK 0x{:02X}",
                    scanCode);
                Trade::HandleKey(kTradeActionCode);
                break;
            }

            if (scanCode == addItemKey) {
                spdlog::debug(
                    "Trade add-item key pressed: DIK 0x{:02X}",
                    scanCode);
                Trade::HandleKey(kAddActionCode);
                break;
            }

            if (scanCode == removeItemKey) {
                spdlog::debug(
                    "Trade remove-item key pressed: DIK 0x{:02X}",
                    scanCode);
                Trade::HandleKey(kRemoveActionCode);
                break;
            }

            if (scanCode == cancelKey) {
                spdlog::debug(
                    "Trade cancel key pressed: DIK 0x{:02X}",
                    scanCode);
                Trade::HandleKey(kCancelActionCode);
                break;
            }

            // Do not double-handle the legacy numpad bindings: those are
            // intentionally handled above through GetAsyncKeyState.
            if (scanCode == goldAddKey && !pollNumpadAdd) {
                const bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                const std::int32_t step = control ? 100 : (shift ? 10 : 1);
                spdlog::info(
                    "Trade gold key pressed: source=ButtonEvent action=add delta={}",
                    step);
                Trade::AdjustGold(step);
                break;
            }

            if (scanCode == goldRemoveKey && !pollNumpadSubtract) {
                const bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                const std::int32_t step = control ? 100 : (shift ? 10 : 1);
                spdlog::info(
                    "Trade gold key pressed: source=ButtonEvent action=remove delta={}",
                    -step);
                Trade::AdjustGold(-step);
                break;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
