#include "SingleBoneDistancePeakHandler.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            SingleBoneDistancePeakHandler::SingleBoneDistancePeakHandler(PeakHandlerParams params, DistancePeakHandlerParams distanceParams, SingleBoneDistancePeakHandlerParams singleParams)
                : DistancePeakHandler(params, distanceParams), actorBone{ singleParams.actorBone }, targetBone{ singleParams.targetBone } {

            }

            float SingleBoneDistancePeakHandler::calculateDistance() {
                return actorBone.distance(targetBone);
            }
        }
    }
}