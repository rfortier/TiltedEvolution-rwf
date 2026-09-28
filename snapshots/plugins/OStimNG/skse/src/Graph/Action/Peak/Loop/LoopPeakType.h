#pragma once

#include "../PeakType.h"
#include "LoopPeakTypeParams.h"

namespace Graph {
    namespace Action {
        namespace Peak {
            class LoopPeakType : public PeakType {
            public:
                LoopPeakType(PeakTypeParams params, LoopPeakTypeParams loopParams);

                virtual Threading::Threads::Peak::PeakHandler* create(Threading::Threads::Peak::PeakHandlerParams params, GameAPI::GameActor actor, GameAPI::GameActor target);

            private:
                int interval;
                bool scaleWithSpeed;
            };
        }
    }
}