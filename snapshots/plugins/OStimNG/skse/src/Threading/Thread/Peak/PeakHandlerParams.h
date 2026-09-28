#pragma once

namespace Threading {
    namespace Threads {
        class NodeHandler;

        namespace Peak {
            struct PeakHandlerParams {
                NodeHandler* nodeHandler;
                std::function<void()> peakListener;
            };
        }  // namespace Peak
    }
}