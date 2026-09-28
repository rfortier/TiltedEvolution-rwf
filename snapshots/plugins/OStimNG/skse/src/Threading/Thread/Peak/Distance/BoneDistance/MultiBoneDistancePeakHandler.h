#pragma once

#include "../DistancePeakHandler.h"
#include "MultiBoneDistancePeakHandlerParams.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            class MultiBoneDistancePeakHandler : public DistancePeakHandler {
            public:
                MultiBoneDistancePeakHandler(PeakHandlerParams params, DistancePeakHandlerParams distanceParams, MultiBoneDistancePeakHandlerParams multiParams);

            protected:
                virtual float calculateDistance();

            private:
                std::vector<GameAPI::GameActorBone> actorBones;
                std::vector<GameAPI::GameActorBone> targetBones;
            };
        }
    }
}