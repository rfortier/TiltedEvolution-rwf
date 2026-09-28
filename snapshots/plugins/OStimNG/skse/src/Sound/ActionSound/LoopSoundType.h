#pragma once

#include "SoundType.h"

namespace Sound {
    class LoopSoundType : public SoundType{
    public:
        inline LoopSoundType(SoundTypeParams params, int delay)
            : SoundType(params), delay{delay} {};

        virtual SoundPlayer* create(int actionIndex, Threading::ThreadActor* actor, Threading::ThreadActor* target);

    private:
        int delay;
    };
}