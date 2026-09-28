#pragma once

#include <RE/Skyrim.h>

namespace AnimSyncTogether
{
    class AnimationInputProbe final
    {
    public:
        static void Install();

    private:
        static bool PlayerNotifyAnimationGraph(
            RE::IAnimationGraphManagerHolder* holder,
            const RE::BSFixedString& eventName);

        static inline REL::Relocation<decltype(PlayerNotifyAnimationGraph)> originalPlayerNotify_;
        static inline bool installed_{ false };
    };
}
