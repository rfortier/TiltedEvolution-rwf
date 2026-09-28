#pragma once

#include "SoundPlayerParams.h"

namespace Sound {
    class SoundPlayer {
    public:
        inline SoundPlayer(SoundPlayerParams& params)
            : actionIndex{params.actionIndex},
              sound{params.sound},
              muteWithActor{params.muteWithActor},
              muteWithTarget{params.muteWithTarget},
              actor{params.actor},
              target{params.target} {};

        virtual ~SoundPlayer();
        
        inline int getActionIndex() { return actionIndex; }
        inline bool hasActionIndex(int index) { return actionIndex == index; }

        virtual void loop() {}
        virtual void peak() {}

    protected:
        int actionIndex;
        GameAPI::GameSound sound;
        bool muteWithActor;
        bool muteWithTarget;
        Threading::ThreadActor* actor;
        Threading::ThreadActor* target;

        void play();
    };

    inline SoundPlayer::~SoundPlayer() {}
}