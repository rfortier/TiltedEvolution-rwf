#pragma once

#include "ActionTag.h"

namespace OStim {
    class ActionTagVisitor {
    public:
        virtual bool visit(ActionTag* action) = 0;
    };
}