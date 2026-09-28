#pragma once

#include "PCH.h"

namespace OStimTogether
{
    class InputHandler final :
        public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static InputHandler& GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* events,
            RE::BSTEventSource<RE::InputEvent*>*) override;

        void Register();

    private:
        InputHandler() = default;

        void EnsureBeforeOStim();
        static bool AddressInsideModule(const void* address, HMODULE module);

        std::optional<std::uint32_t> _suppressedSceneStartKey;
    };
}
