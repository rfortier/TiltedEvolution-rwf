#pragma once

#include "SoundPlayer.h"

#include "Graph/Action/Action.h"

namespace Sound {
    class PeakSoundPlayer : public SoundPlayer {
    public:
        PeakSoundPlayer(SoundPlayerParams params);

        virtual void loop();

    private:
        void peak(::actionIndex index);
    };
}