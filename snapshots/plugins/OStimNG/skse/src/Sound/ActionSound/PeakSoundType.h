#pragma once

#include "SoundType.h"

namespace Sound {
    class PeakSoundType : public SoundType {
    public:
        inline PeakSoundType(SoundTypeParams params) : SoundType(params) {}

        virtual SoundPlayer* create(int actionIndex, Threading::ThreadActor* actor, Threading::ThreadActor* target);
    };
}