#include "MultiBoneDistancePeakHandler.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            MultiBoneDistancePeakHandler::MultiBoneDistancePeakHandler(PeakHandlerParams params, DistancePeakHandlerParams distanceParams, MultiBoneDistancePeakHandlerParams multiParams)
                : DistancePeakHandler(params, distanceParams), actorBones{ multiParams.actorBones }, targetBones{ multiParams.targetBones } {

            }

            float MultiBoneDistancePeakHandler::calculateDistance() {
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
    }
}