#pragma once

#include "SoundPlayer.h"

namespace Sound {
    class BoneDistanceSoundPlayer : public SoundPlayer{
    public:
        inline BoneDistanceSoundPlayer(SoundPlayerParams params, bool inverse, int minInterval, int maxInterval)
            : SoundPlayer(params), inverse{inverse}, minInterval{minInterval}, maxInterval{maxInterval} {}
        virtual ~BoneDistanceSoundPlayer();

        virtual void loop();

    protected:
        virtual float calculateDistance() = 0;

    private:
        bool inverse;
        int minInterval;
        int maxInterval;

        int initialCooldown = 1000;

        float minDistance = -1;
        float maxDistance = -1;
        float distanceThreshold = -1;
        bool inThreshold = false;

        int timeSinceLastTrigger = 0;

        void calculateThreshold();
        void trigger();
    };

    inline BoneDistanceSoundPlayer::~BoneDistanceSoundPlayer() {}
}