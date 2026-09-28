#pragma once

namespace TradeTogether
{
    class InputEventSink final : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static InputEventSink* GetSingleton();
        void Register();
        void ReloadConfig();

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_event,
            RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

    private:
        // Internal action identifiers expected by Trade.cpp. User-facing keys
        // are mapped to these values by Input.cpp.
        static constexpr std::uint32_t kTradeActionCode = 0x40;
        static constexpr std::uint32_t kAddActionCode = 0x12;
        static constexpr std::uint32_t kRemoveActionCode = 0xD3;
        static constexpr std::uint32_t kCancelActionCode = 0x0F;

        std::atomic<std::uint32_t> _tradeKey{ 0x14 };
        std::atomic<std::uint32_t> _addItemKey{ 0xD2 };
        std::atomic<std::uint32_t> _removeItemKey{ 0xD3 };
        std::atomic<std::uint32_t> _goldAddKey{ 0x4E };
        std::atomic<std::uint32_t> _goldRemoveKey{ 0x4A };
        std::atomic<std::uint32_t> _cancelKey{ 0x0F };
    };
}
