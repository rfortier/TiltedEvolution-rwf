#pragma once

#include "../PeakHandler.h"
#include "DistancePeakHandlerParams.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            class DistancePeakHandler : public PeakHandler {
            public:
                DistancePeakHandler(PeakHandlerParams params, DistancePeakHandlerParams distanceParams);
                virtual ~DistancePeakHandler();

            protected:
                virtual void loop();
                virtual float calculateDistance() = 0;

            private:
                bool inverse;

                int initialCooldown = 1000;

                float minDistance = -1;
                float maxDistance = -1;
                float minThreshold = -1;
                float maxThreshold = -1;
                bool movingIn = false;

                void calculateThresholds();
            };

            inline DistancePeakHandler::~DistancePeakHandler() {}
        }
    }
}