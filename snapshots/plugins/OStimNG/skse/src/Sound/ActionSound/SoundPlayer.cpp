#include "SoundType.h"

#include "Core/ThreadActor.h"
#include "MCM/MCMTable.h"

namespace Sound {
    void SoundPlayer::play() {
        if (muteWithActor && actor->isMuted()) {
            return;
        }

        if (muteWithTarget && target->isMuted()) {
            return;
        }

        sound.play(actor->getActor(), MCM::MCMTable::getSoundVolume());
    }
}