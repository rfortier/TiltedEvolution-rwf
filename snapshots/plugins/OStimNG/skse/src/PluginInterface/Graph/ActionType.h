#pragma once

#include "ActionActor.h"
#include "ActionTag.h"
#include "ActionTagVisitor.h"
#include "Role.h"

namespace OStim {
    class ActionType {
    public:
        // --- ABI version 1 ---
        virtual const char* getActionID() = 0;

        virtual bool hasTag(const char* tag) = 0;
        virtual uint32_t getTagCount() = 0;
        virtual ActionTag* getTag(uint32_t index) = 0;
        virtual void forEachTag(ActionTagVisitor* visitor) = 0;

        // --- ABI version 0x07050003 ---
        virtual ActionActor* getActor(Role role) = 0;
    };
}