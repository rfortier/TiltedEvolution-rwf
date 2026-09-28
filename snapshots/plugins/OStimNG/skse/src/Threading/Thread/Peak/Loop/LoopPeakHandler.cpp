#include "LoopPeakHandler.h"

#include "../../NodeHandler.h"
#include "Core/Thread.h"
#include "Util/Constants.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            LoopPeakHandler::LoopPeakHandler(PeakHandlerParams params, LoopPeakHandlerParams loopParams)
                : PeakHandler(params), interval{loopParams.interval}, nodeHandler{params.nodeHandler} {
                countdown = interval;

                if (loopParams.scaleWithSpeed) {
                    nodeHandler->registerSpeedChangedListener([this]() { speedChanged(); });
                }
            }

            void LoopPeakHandler::loop() {
                countdown -= static_cast<int>(Constants::LOOP_TIME_MILLISECONDS * scale);

                if (countdown > 0) {
                    return;
                }

                peak();
                countdown = interval;
            }

            void LoopPeakHandler::speedChanged() {
                scale = nodeHandler->getThread()->getCurrentNodeInternal()->speeds[nodeHandler->getThread()->getCurrentSpeed()].displaySpeed;
            }
        }
    }
}