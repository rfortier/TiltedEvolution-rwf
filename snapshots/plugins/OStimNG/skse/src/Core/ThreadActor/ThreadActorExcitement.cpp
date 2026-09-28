#include "Core/ThreadActor.h"

#include "Core/Thread.h"

#include "MCM/MCMTable.h"
#include "Util/APITable.h"
#include "Util/Constants.h"

namespace Threading {
    void ThreadActor::setExcitement(float value) {
        excitement = value;
        Util::APITable::getExcitementFaction().setRank(actor, (int)excitement);
    }

    void ThreadActor::setMaxExcitement(float max) {
        maxExcitement = max;
        if (maxExcitement < 100.0f) {
            setTimeUntilClimax(-1.0f);
        }
    }

    void ThreadActor::addExcitement(float value, bool respectMultiplier) {
        if (respectMultiplier) {
            excitement += value * excitementMultiplier;
        } else {
            excitement += value;
        }

        if (excitement < 0) {
            excitement = 0;
        } else if (excitement > 100) {
            excitement = 100;
        }

        Util::APITable::getExcitementFaction().setRank(actor, (int)excitement);
    }

    void ThreadActor::loopExcitement() {
        if (awaitingOrgasm || awaitingClimax || awaitingClimaxInner) {
            return;
        }

        if (excitement > maxExcitement) {
            if (excitementDecayCooldown > 0.0f) {
                excitementDecayCooldown -= Constants::LOOP_TIME_MILLISECONDS;
            } else {
                excitement -= loopExcitementDecay;
                if (excitement < maxExcitement) {
                    excitement = maxExcitement;
                }
                Util::APITable::getExcitementFaction().setRank(actor, (int)excitement);
            }
        } else {  // increase excitement
            excitement += loopExcitementInc;
            if (excitement > maxExcitement) {
                excitement = maxExcitement;
            }
            Util::APITable::getExcitementFaction().setRank(actor, (int)excitement);
            excitementDecayCooldown = MCM::MCMTable::getExcitementDecayGracePeriod();

            if (maxExcitement >= 100.0f) {
                setTimeUntilClimax((100.0f - excitement) / (baseExcitementInc * excitementMultiplier));
            }
        }

        if (excitement >= 100) {
            orgasm(false);
        }
    }

    void ThreadActor::changeSpeedExcitement() {
        recalculateLoopExcitement();
    }

    void ThreadActor::setExcitementMultiplier(float multiplier) {
        excitementMultiplier = multiplier;
        recalculateLoopExcitement();
    }

    void ThreadActor::recalculateLoopExcitement() {
        float speedMod = 1.0 + static_cast<float>(speed) / static_cast<float>(thread->getCurrentNodeInternal()->speeds.size());
        setLoopExcitementInc(baseExcitementInc * excitementMultiplier * speedMod * Constants::LOOP_TIME_SECONDS);
    }
}