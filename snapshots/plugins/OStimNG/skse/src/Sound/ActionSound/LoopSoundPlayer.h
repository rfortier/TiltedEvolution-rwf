#pragma once

#include "SoundPlayer.h"

namespace Sound {
    class LoopSoundPlayer : public SoundPlayer {
    public:
        inline LoopSoundPlayer(SoundPlayerParams params, int delay)
            : SoundPlayer(params), delay{delay} {
            currentDelay = delay;
            play();
        }

        inline ~LoopSoundPlayer() {
            if (sound.isPlaying()) {
                sound.stop();
            }
        }

        virtual void loop();

    private:
        int delay;

        int currentDelay = 0;
    };
}