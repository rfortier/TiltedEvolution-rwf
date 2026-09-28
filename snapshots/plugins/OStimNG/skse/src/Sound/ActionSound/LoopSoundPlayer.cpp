#include "LoopSoundPlayer.h"

#include "Util/Constants.h"

namespace Sound {
    void LoopSoundPlayer::loop() {
        if (sound.isPlaying()) {
            return;
        }

        if ((currentDelay -= Constants::LOOP_TIME_MILLISECONDS) > 0) {
            return;
        }

        // the sound engine takes about 50-100 ms before the sound actually plays
        // so we add 150 to the delay in order to not start a second instance of the sound because we do not
        // yet know the first one is running
        currentDelay = delay + 150;
        play();
    }
}