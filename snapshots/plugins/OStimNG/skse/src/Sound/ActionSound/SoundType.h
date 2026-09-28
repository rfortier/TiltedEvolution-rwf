#pragma once

#include "SoundPlayer.h"

#include "GameAPI/GameSound.h"

namespace Sound {
    struct SoundTypeParams {
        GameAPI::GameSound sound;
        bool muteWithActor = false;
        bool muteWithTarget = false;
        bool playerThreadOnly = false;
    };

    class SoundType {
    public:
        static SoundType* fromJson(std::string path, json json);

        inline SoundType(SoundTypeParams params) : sound{params.sound}, muteWithActor{params.muteWithActor}, muteWithTarget{params.muteWithTarget}, playerThreadOnly{params.playerThreadOnly} {}

        virtual SoundPlayer* create(int actionIndex, Threading::ThreadActor* actor, Threading::ThreadActor* target) = 0;

        inline bool playPlayerThreadOnly() { return playerThreadOnly; }

    protected:
        GameAPI::GameSound sound;
        bool muteWithActor;
        bool muteWithTarget;
        bool playerThreadOnly;
    };
}