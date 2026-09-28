#include "Core/Thread.h"

namespace Threading {
    bool Thread::isAnyActorAwaitingClimax() {
        for (auto& [index, actor] : m_actors) {
            if (actor.getAwaitingClimax()) {
                return true;
            }
        }
        return false;
    }
}