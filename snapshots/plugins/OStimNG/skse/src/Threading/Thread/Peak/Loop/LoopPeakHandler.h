#pragma once

#include "LoopPeakHandlerParams.h"
#include "../PeakHandler.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            class LoopPeakHandler : public PeakHandler {
            public:
                LoopPeakHandler(PeakHandlerParams params, LoopPeakHandlerParams loopParams);

            protected:
                virtual void loop();

            private:
                int interval;
                int countdown;
                float scale = 1.0f;
                NodeHandler* nodeHandler;

                void speedChanged();
            };
        }
    }
}