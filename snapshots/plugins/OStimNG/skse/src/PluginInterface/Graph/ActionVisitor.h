#pragma once

#include "Action.h"

namespace OStim {
    class ActionVisitor {
    public:
        virtual bool visit(Action* action) = 0;
    };
}