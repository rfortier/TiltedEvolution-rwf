#include "NodeHandler.h"

#include "Core/Thread.h"
#include "Util/EventUtil.h"

namespace Threading {
    namespace Threads {
        NodeHandler::NodeHandler(Threading::Thread* thread) : thread{thread} {
            thread->registerLoopListener([this]() { loop(); });
            thread->registerPeakListener([this](actionIndex action) { peak(action); });
            thread->registerNodeChangedListener([this]() { nodeChanged(); });
            thread->registerSpeedChangedListener([this]() { speedChanged(); });
            thread->registerClimaxListener([this](Threading::ThreadActor* actor) { climax(actor); });
            thread->registerThreadEndListener([this]() { threadEnd(); });
        }

        void NodeHandler::handlePeakAnnotation(actionIndex index) {
            for (Graph::Action::Action& action : thread->getCurrentNodeInternal()->actions) {
                if (action.index == index) {
                    if (!action.doPeaks || !action.peaksAnnotated) {
                        return;
                    }
                    break;
                }
            }

            sendPeak(index);
        }

        void NodeHandler::sendPeak(actionIndex action) {
            thread->sendPeak(action);
        }


        void NodeHandler::loop() {
            EventUtil::invokeListeners(loopListeners);
        }

        void NodeHandler::peak(actionIndex action) {
            EventUtil::invokeListeners(peakListeners, action);
        }

        void NodeHandler::speedChanged() {
            EventUtil::invokeListeners(speedChangedListeners);
        }

        void NodeHandler::nodeChanged() {
            EventUtil::invokeListeners(nodeChangedListeners);

            loopListeners.clear();
            peakListeners.clear();
            speedChangedListeners.clear();
            nodeChangedListeners.clear();
            climaxListeners.clear();
            threadEndListeners.clear();

            for (Graph::Action::Action& action : thread->getCurrentNodeInternal()->actions) {
                if (action.attributes->peakType && action.doPeaks && !action.peaksAnnotated) {
                    actionIndex index = action.index;
                    Peak::PeakHandlerParams params{this, [this, index]() { sendPeak(index); }};
                    Threading::ThreadActor* actor = thread->GetActor(action.roles.actor);
                    Threading::ThreadActor* target = thread->GetActor(action.roles.target);
                    if (actor && target) {
                        action.attributes->peakType->create(params, actor->getActor(), target->getActor());
                    }
                }
            }
        }

        void NodeHandler::climax(Threading::ThreadActor* actor) {
            EventUtil::invokeListeners(climaxListeners, actor);
        }

        void NodeHandler::threadEnd() {
            EventUtil::invokeListeners(threadEndListeners);
            delete this;
        }
    }
}