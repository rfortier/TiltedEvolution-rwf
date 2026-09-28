#include "LoopPeakType.h"

#include "Threading/Thread/Peak/Loop/LoopPeakHandler.h"

namespace Graph {
    namespace Action {
        namespace Peak {
            LoopPeakType::LoopPeakType(PeakTypeParams params, LoopPeakTypeParams loopParams)
                : PeakType(params), interval{loopParams.interval}, scaleWithSpeed{loopParams.scaleWithSpeed} {

            }

            Threading::Threads::Peak::PeakHandler* LoopPeakType::create(Threading::Threads::Peak::PeakHandlerParams params, GameAPI::GameActor actor, GameAPI::GameActor target) {
                return new Threading::Threads::Peak::LoopPeakHandler(params, {interval, scaleWithSpeed});
            }
        }
    }
}