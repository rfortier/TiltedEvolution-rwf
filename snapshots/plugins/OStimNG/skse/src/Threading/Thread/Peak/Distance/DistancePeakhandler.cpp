#include "DistancePeakHandler.h"

#include "Util/Constants.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            DistancePeakHandler::DistancePeakHandler(PeakHandlerParams params, DistancePeakHandlerParams distanceParams)
                : PeakHandler(params), inverse{ distanceParams.inverse } {

            }

            void DistancePeakHandler::loop() {
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
                        calculateThresholds();
                    }
                }

                if (maxDistance < 0 || distance > maxDistance) {
                    maxDistance = distance;
                    calculateThresholds();
                }

                if (movingIn) {
                    bool isIn = distance < minThreshold;
                    if (isIn) {
                        if (!inverse) {
                            peak();
                        }
                        movingIn = false;
                    }
                } else {
                    bool isOut = distance > maxThreshold;
                    if (isOut) {
                        if (inverse) {
                            peak();
                        }
                        movingIn = true;
                    }
                }
            }

            void DistancePeakHandler::calculateThresholds() {
                minThreshold = minDistance + (maxDistance - minDistance) * 0.1;
                maxThreshold = minDistance + (maxDistance - minDistance) * 0.9;
            }
        }
    }
}