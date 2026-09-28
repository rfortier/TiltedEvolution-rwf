#include "PeakSoundType.h"

#include "PeakSoundPlayer.h"

namespace Sound {
    SoundPlayer* PeakSoundType::create(int actionIndex, Threading::ThreadActor* actor, Threading::ThreadActor* target) {
        return new PeakSoundPlayer({actionIndex, sound, muteWithActor, muteWithTarget, actor, target});
    }
}