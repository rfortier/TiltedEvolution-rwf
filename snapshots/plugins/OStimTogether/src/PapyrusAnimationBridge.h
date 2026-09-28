#pragma once

#include "PCH.h"

namespace OStimTogether
{
    class PapyrusAnimationBridge
    {
    public:
        static PapyrusAnimationBridge& GetSingleton();

        // Must be called on Skyrim's game thread.
        //
        // Uses the native Papyrus Debug.SendAnimationEvent(ObjectReference,
        // string) path as a forced animation-event probe.
        bool SendForcedAnimationEvent(
            RE::Actor* actor,
            std::string_view eventName);

        // Generic OStim equip-object bridge for optional integrations.
        // The core treats objectType as opaque addon data.
        bool SetOStimObjectState(
            RE::Actor* actor,
            std::string_view objectType,
            bool equipped);

    private:
        PapyrusAnimationBridge() = default;
    };
}
