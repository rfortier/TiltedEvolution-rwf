#include "PeakSoundPlayer.h"

#include "Core/Thread.h"
#include "Core/ThreadActor.h"

namespace Sound {
    PeakSoundPlayer::PeakSoundPlayer(SoundPlayerParams params) : SoundPlayer(params) {
        params.actor->getThread()->getNodeHandler()->registerPeakListener([this](::actionIndex index) { peak(index); });
    }

    void PeakSoundPlayer::loop() {}

    void PeakSoundPlayer::peak(::actionIndex index) {
        if (index != actionIndex) {
            return;
        }

        play();
    }
}