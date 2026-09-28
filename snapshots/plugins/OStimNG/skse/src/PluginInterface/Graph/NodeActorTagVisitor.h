#pragma once

#include "NodeActorTag.h"

namespace OStim {
    class NodeActorTagVisitor {
    public:
        virtual bool visit(NodeActorTag* action) = 0;
    };
}