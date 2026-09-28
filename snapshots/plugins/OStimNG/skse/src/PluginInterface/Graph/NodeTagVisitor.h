#pragma once

#include "NodeTag.h"

namespace OStim {
    class NodeTagVisitor {
    public:
        virtual bool visit(NodeTag* action) = 0;
    };
}  // namespace OStim