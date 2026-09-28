#include "BoneDistanceSoundPlayer.h"

#include "Util/Constants.h"

namespace Sound {
    void BoneDistanceSoundPlayer::loop() {
        // the smooth transitions from animations can sometimes bork up calculcations
        // so we have to wait for half a second before doing anything
        if (initialCooldown > 0) {
            initialCooldown -= Constants::LOOP_TIME_MILLISECONDS;
            return;
        }

        float distance = calculateDistance();

        if (minDistance < 0 || distance < minDistance) {
            minDistance = distance;
            if (maxDistance > 0) {
                calculateThreshold();
            }
        }

        if (maxDistance < 0 || distance > maxDistance) {
            maxDistance = distance;
            calculateThreshold();
        }

        bool newInThreshold = inverse ? distance > distanceThreshold : distance < distanceThreshold;
        if (inThreshold && !newInThreshold) {
            trigger();
        }

        inThreshold = newInThreshold;

        timeSinceLastTrigger += Constants::LOOP_TIME_MILLISECONDS;
    }

    void BoneDistanceSoundPlayer::calculateThreshold() {
        distanceThreshold = minDistance + (maxDistance - minDistance) * (inverse ? 0.9 : 0.1);
    }

    void BoneDistanceSoundPlayer::trigger() {
        if (timeSinceLastTrigger < minInterval || (maxInterval != 0 && timeSinceLastTrigger > maxInterval)) {
            timeSinceLastTrigger = 0;
            return;
        }

        play();
        timeSinceLastTrigger = 0;
    }
}