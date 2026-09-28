#pragma once

#include "Thread.h"
#include "ThreadActor.h"

namespace OStim {
    class ThreadActorEventListener {
    public:
        virtual void listen(Thread* thread, ThreadActor* actor) = 0;
    };
}  // namespace OStim