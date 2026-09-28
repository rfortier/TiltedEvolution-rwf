#pragma once

#include "NodeActorTag.h"
#include "NodeActorTagVisitor.h"

namespace OStim {
    class NodeActor {
    public:
        // --- ABI version 1 ---
        virtual bool hasTag(const char* tag) = 0;
        virtual uint32_t getTagCount() = 0;
        virtual NodeActorTag* getTag(uint32_t index) = 0;
        virtual void forEachTag(NodeActorTagVisitor* visitor) = 0;
    };
}