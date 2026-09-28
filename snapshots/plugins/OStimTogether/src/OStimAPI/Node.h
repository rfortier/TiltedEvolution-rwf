#pragma once
#include <cstdint>

namespace OStim
{
    class NodeActor;
    class NodeActorVisitor;
    class NodeTag;
    class NodeTagVisitor;
    class Action;
    class ActionVisitor;

    class Node
    {
    public:
        // Exact OStim Graph/Node ABI v1. No virtual destructor.
        virtual const char* getNodeID() = 0;
        virtual std::uint32_t getActorCount() = 0;
        virtual NodeActor* getActor(std::uint32_t index) = 0;
        virtual void forEachActor(NodeActorVisitor* visitor) = 0;
        virtual bool hasTag(const char* tag) = 0;
        virtual std::uint32_t getTagCount() = 0;
        virtual NodeTag* getTag(std::uint32_t index) = 0;
        virtual void forEachTag(NodeTagVisitor* visitor) = 0;
        virtual bool hasAction(const char* action) = 0;
        virtual std::uint32_t getActionCount() = 0;
        virtual Action* getAction(std::uint32_t index) = 0;
        virtual void forEachAction(ActionVisitor* visitor) = 0;
    };
}
