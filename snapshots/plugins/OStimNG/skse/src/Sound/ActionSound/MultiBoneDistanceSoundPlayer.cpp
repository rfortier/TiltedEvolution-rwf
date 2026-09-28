#include "MultiBoneDistanceSoundPlayer.h"

namespace Sound {
    float MultiBoneDistanceSoundPlayer::calculateDistance() {
        float distance = std::numeric_limits<float>::quiet_NaN();
        for (GameAPI::GameActorBone& actorBone : actorBones) {
            for (GameAPI::GameActorBone& targetBone : targetBones) {
                float boneDistance = actorBone.distance(targetBone);
                if (std::isnan(distance) || boneDistance < distance) {
                    distance = boneDistance;
                }
            }
        }

        return distance;
    }
}