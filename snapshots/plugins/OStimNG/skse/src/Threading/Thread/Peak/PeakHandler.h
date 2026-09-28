#pragma once

#include "PeakHandlerParams.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            class PeakHandler {
            public:
                PeakHandler(PeakHandlerParams params);
                virtual ~PeakHandler();

            protected:
                virtual void loop() = 0;
                inline void peak() { peakListener(); };

            private:
                std::function<void()> peakListener;

                inline void nodeChanged() { delete this; }
                inline void threadEnd() { delete this; }
            };

            inline PeakHandler::~PeakHandler() {}
        }  // namespace Peak
    }
}