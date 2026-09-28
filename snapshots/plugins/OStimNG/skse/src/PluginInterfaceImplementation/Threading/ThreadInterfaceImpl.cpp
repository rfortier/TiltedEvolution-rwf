#include "ThreadInterfaceImpl.h"

#include "ThreadBuilderImpl.h"
#include "../InterfaceMapImpl.h"

#include "Core/ThreadManager.h"

namespace Interface {
    uint32_t ThreadInterfaceImpl::getVersion() {
        return SKSE::PluginDeclaration::GetSingleton()->GetVersion().pack();
    }


    OStim::Thread* ThreadInterfaceImpl::getThread(int32_t threadID) {
        return Threading::ThreadManager::GetSingleton()->GetThread(threadID);
    }


    void ThreadInterfaceImpl::registerThreadStartListener(OStim::ThreadEventListener* listener) {
        Threading::ThreadManager::GetSingleton()->registerThreadStartListener([listener](Threading::Thread* thread) { listener->listen(thread); });
    }

    void ThreadInterfaceImpl::registerSpeedChangedListener(OStim::ThreadEventListener* listener) {
        Threading::ThreadManager::GetSingleton()->registerSpeedChangedListener([listener](Threading::Thread* thread) { listener->listen(thread); });
    }

    void ThreadInterfaceImpl::registerNodeChangedListener(OStim::ThreadEventListener* listener) {
        Threading::ThreadManager::GetSingleton()->registerNodeChangedListener([listener](Threading::Thread* thread) { listener->listen(thread); });
    }

    void ThreadInterfaceImpl::registerClimaxListener(OStim::ThreadActorEventListener* listener) {
        Threading::ThreadManager::GetSingleton()->registerClimaxListener([listener](Threading::Thread* thread, Threading::ThreadActor* actor) { listener->listen(thread, actor); });
    }

    void ThreadInterfaceImpl::registerThreadStopListener(OStim::ThreadEventListener* listener) {
        Threading::ThreadManager::GetSingleton()->registerThreadEndListener([listener](Threading::Thread* thread) { listener->listen(thread); });
    }


    OStim::ThreadBuilder* ThreadInterfaceImpl::createThreadBuilder(uint32_t actorCount, void** actors) {
        std::vector<GameAPI::GameActor> gameActors;

        for (int i = 0; i < actorCount; i++) {
            GameAPI::GameActor actor = *(actors + i);
            if (!Threading::isEligible(actor)) {
                return nullptr;
            }
            gameActors.push_back(actor);
        }

        return new ThreadBuilderImpl(gameActors);
    }

    OStim::ThreadActor* ThreadInterfaceImpl::getActor(void* gameActor) {
        return Threading::ThreadManager::GetSingleton()->findActor(gameActor);
    }
}