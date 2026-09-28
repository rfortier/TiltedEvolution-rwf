#pragma once

#include "../DistancePeakHandler.h"
#include "SingleBoneDistancePeakHandlerParams.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            class SingleBoneDistancePeakHandler : public DistancePeakHandler {
            public:
                SingleBoneDistancePeakHandler(PeakHandlerParams params, DistancePeakHandlerParams distanceParams, SingleBoneDistancePeakHandlerParams singleParams);

            protected:
                virtual float calculateDistance();

            private:
                GameAPI::GameActorBone actorBone;
                GameAPI::GameActorBone targetBone;
            };
        }
    }
}