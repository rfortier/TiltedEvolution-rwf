#include "LoopSoundType.h"

#include "LoopSoundPlayer.h"

#include "Core/ThreadActor.h"

namespace Sound {
    SoundPlayer* LoopSoundType::create(int actionIndex, Threading::ThreadActor* actor, Threading::ThreadActor* target) {
        return new LoopSoundPlayer({actionIndex, sound, muteWithActor, muteWithTarget, actor, target}, delay);
    }
}