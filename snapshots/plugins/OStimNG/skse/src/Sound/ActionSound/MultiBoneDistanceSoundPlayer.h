#pragma once

#include "BoneDistanceSoundPlayer.h"

namespace Sound {
    class MultiBoneDistanceSoundPlayer : public BoneDistanceSoundPlayer {
    public:
        inline MultiBoneDistanceSoundPlayer(SoundPlayerParams params, bool inverse, int minInterval, int maxInterval, std::vector<GameAPI::GameActorBone> actorBones, std::vector<GameAPI::GameActorBone> targetBones)
            : BoneDistanceSoundPlayer(params, inverse, minInterval, maxInterval), actorBones{actorBones}, targetBones{targetBones} {}

    protected:
        virtual float calculateDistance();

    private:
        std::vector<GameAPI::GameActorBone> actorBones;
        std::vector<GameAPI::GameActorBone> targetBones;
    };
}