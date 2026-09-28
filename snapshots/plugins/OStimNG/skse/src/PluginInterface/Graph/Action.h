#pragma once

#include "ActionType.h"
#include "Role.h"

namespace OStim {
    class Action {
    public:
        // --- ABI version 1 ---
        virtual ActionType* getType() = 0;

        virtual int32_t getActor(Role role) = 0;
    };
}