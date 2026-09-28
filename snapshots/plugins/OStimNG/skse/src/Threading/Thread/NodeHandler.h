#pragma once

#include "Graph/Action/Action.h"

namespace Threading {
    class Thread;
}

namespace Threading {
    namespace Threads {
        class NodeHandler {
        public:
            NodeHandler(Threading::Thread* thread);

            inline Threading::Thread* getThread() { return thread; }

            void handlePeakAnnotation(actionIndex index);

        private:
            Threading::Thread* thread = nullptr;

            void sendPeak(actionIndex action);

            void loop();
            void peak(actionIndex action);
            void speedChanged();
            void nodeChanged();
            void climax(Threading::ThreadActor* actor);
            void threadEnd();

#pragma region events
        public:
            inline void registerLoopListener(std::function<void()> listener) { loopListeners.push_back(listener); }
            inline void registerPeakListener(std::function<void(actionIndex)> listener) { peakListeners.push_back(listener); }
            inline void registerSpeedChangedListener(std::function<void()> listener) { speedChangedListeners.push_back(listener); }
            inline void registerNodeChangedListener(std::function<void()> listener) { nodeChangedListeners.push_back(listener); }
            inline void registerClimaxListener(std::function<void(Threading::ThreadActor*)> listener) { climaxListeners.push_back(listener); }
            inline void registerThreadEndListener(std::function<void()> listener) { threadEndListeners.push_back(listener); }

        private:
            std::vector<std::function<void()>> loopListeners;
            std::vector<std::function<void(actionIndex)>> peakListeners;
            std::vector<std::function<void()>> speedChangedListeners;
            std::vector<std::function<void()>> nodeChangedListeners;
            std::vector<std::function<void(Threading::ThreadActor*)>> climaxListeners;
            std::vector<std::function<void()>> threadEndListeners;
#pragma endregion
        };
    }
}