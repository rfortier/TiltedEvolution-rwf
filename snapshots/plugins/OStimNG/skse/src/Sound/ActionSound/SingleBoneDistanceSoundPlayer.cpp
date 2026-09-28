#include "SingleBoneDistanceSoundPlayer.h"

#include <math.h>

namespace Sound {
    float SingleBoneDistanceSoundPlayer::calculateDistance() {
        return actorBone.distance(targetBone);
    }
}