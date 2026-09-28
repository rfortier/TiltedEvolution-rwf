#pragma once

#include "Action.h"
#include "ActionVisitor.h"
#include "NodeActor.h"
#include "NodeActorVisitor.h"
#include "NodeTag.h"
#include "NodeTagVisitor.h"

#include "../Furniture/FurnitureType.h"

namespace OStim {
    class Node {
    public:
        // --- ABI version 1 ---
        virtual const char* getNodeID() = 0; // this will always be fully lowercase

        virtual uint32_t getActorCount() = 0;
        virtual NodeActor* getActor(uint32_t index) = 0;
        virtual void forEachActor(NodeActorVisitor* visitor) = 0;

        virtual bool hasTag(const char* tag) = 0;
        virtual uint32_t getTagCount() = 0;
        virtual NodeTag* getTag(uint32_t index) = 0;
        virtual void forEachTag(NodeTagVisitor* visitor) = 0;

        virtual bool hasAction(const char* action) = 0;
        virtual uint32_t getActionCount() = 0;
        virtual Action* getAction(uint32_t index) = 0;
        virtual void forEachAction(ActionVisitor* visitor) = 0;

        // --- ABI version 0x07050010 ---
        virtual bool isValid(uint32_t actorCount, void** actors) = 0;
        
        virtual FurnitureType* getFurnitureType() = 0;
    };
}