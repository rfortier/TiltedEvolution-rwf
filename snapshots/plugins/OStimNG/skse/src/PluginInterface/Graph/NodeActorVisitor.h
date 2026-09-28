#pragma once

#include "NodeActor.h"

namespace OStim {
    class NodeActorVisitor {
    public:
        virtual bool visit(NodeActor* action) = 0;
    };
}