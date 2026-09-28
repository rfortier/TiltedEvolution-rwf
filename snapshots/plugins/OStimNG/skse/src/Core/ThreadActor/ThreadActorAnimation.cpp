#include "Core/ThreadActor.h"

#include "MCM/MCMTable.h"

namespace Threading {
    void ThreadActor::playAnimation(Graph::Speed& speed) {
        if (!graphActor) {
            return;
        }

        actor.playAnimation(speed.animation + "_" + std::to_string(graphActor->animationIndex), speed.playbackSpeed);
    }

    void ThreadActor::bendSchlong() {
        if (!MCM::MCMTable::isSchlongBendingDisabled()) {
            int totalBend = sosBend + sosOffset;
            if (totalBend > 9) {
                totalBend = 9;
            } else if (totalBend < -10) {
                totalBend = -10;
            }

            // TODO how to do this with GameAPI?
            auto sosString = totalBend == -10 ? "SOSFlaccid" : "SOSBend" + std::to_string(totalBend);
            actor.playAnimation(sosString);
        }
    }

    void ThreadActor::scale() {
        if (MCM::MCMTable::isScalingDisabled()) {
            float newScale = scaleBefore * scaleMult;
            if (!actor.isScale(newScale)) {
                actor.setScale(newScale);
            }
            return;
        }

        if (!graphActor) {
            return;
        }

        float newScale = (graphActor->scale * scaleMult) / (actor.getHeight() * rmHeight);
        if (!heelOffsetRemoved && heelOffset != 0) {
            newScale *= graphActor->scaleHeight / (graphActor->scaleHeight + heelOffset);
        }

        // setscale resets 3BA physics, so we don't do it if the actor already has the desired scale
        if (!actor.isScale(newScale)) {
            actor.setScale(newScale);
        }
    }
}