#pragma once

#include "ThreadActor.h"

namespace OStim {
    class ThreadActorVisitor {
    public:
        virtual bool visit(ThreadActor* actor) = 0;
    };
}