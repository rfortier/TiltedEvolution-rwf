#pragma once

#include "PCH.h"

namespace OStimTogether
{
    // Compatibility shim for the 0.36.x mirror-only OCum architecture.
    //
    // SKEEOverlayRefresh used to rebuild/materialize RaceMenu overlay geometry
    // from native code. That behavior was deliberately removed in 0.36.0:
    // OCum/RaceMenu exclusively own rendering of the real local player while
    // OStim Together only mirrors the resulting state to the STR proxy.
    //
    // PapyrusAnimationBridge still calls Queue() after an OStim equip-object
    // transition. Keep that source-level call harmless until the bridge is
    // cleaned up separately; do not resurrect the old RaceMenu rebuild path.
    class SKEEOverlayRefresh
    {
    public:
        static void Queue(
            RE::Actor*,
            std::string_view) noexcept
        {}
    };
}
