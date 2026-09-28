#pragma once

#include "BoneDistanceSoundPlayer.h"

#include "GameAPI/GameActorBone.h"

namespace Sound {
    class SingleBoneDistanceSoundPlayer : public BoneDistanceSoundPlayer {
    public:
        inline SingleBoneDistanceSoundPlayer(SoundPlayerParams params, bool inverse, int minInterval, int maxInterval, GameAPI::GameActorBone actorBone, GameAPI::GameActorBone targetBone)
            : BoneDistanceSoundPlayer(params, inverse, minInterval, maxInterval), actorBone{actorBone}, targetBone{targetBone} {}

    protected:
        virtual float calculateDistance();

    private:
        GameAPI::GameActorBone actorBone;
        GameAPI::GameActorBone targetBone;
    };
}