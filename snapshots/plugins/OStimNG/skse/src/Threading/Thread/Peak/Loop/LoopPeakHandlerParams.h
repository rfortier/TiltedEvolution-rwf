#pragma once

namespace Threading {
    namespace Threads {
        namespace Peak {
            struct LoopPeakHandlerParams {
                int interval = 1000;
                bool scaleWithSpeed = true;
            };
        }
    }
}