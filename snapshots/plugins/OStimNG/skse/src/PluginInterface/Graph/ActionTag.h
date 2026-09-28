#pragma once

#include "ActionActor.h"
#include "Role.h"

namespace OStim {
    class ActionTag {
    public:
        // --- ABI version 1 ---
        virtual const char* getTagID() = 0;

        // --- ABI version 0x07050003 ---
        virtual ActionActor* getActor(Role role) = 0;
    };
}