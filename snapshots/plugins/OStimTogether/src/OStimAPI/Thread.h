#pragma once
#include <cstdint>
#include "ThreadActor.h"
#include "Node.h"

namespace OStim
{
    class FurnitureType;

    class Thread
    {
    public:
        virtual std::int32_t getThreadID() = 0;
        virtual bool isPlayerThread() = 0;
        virtual std::uint32_t getActorCount() = 0;
        virtual ThreadActor* getActor(std::uint32_t position) = 0;

        // Exact public ABI v1 prefix shared by OStim 7.4c and 7.5b.
        virtual void forEachThreadActor(void* visitor) = 0;
        virtual Node* getCurrentNode() = 0;

        // Public ABI v3 additions exposed by OStim 7.5b. Call these only when
        // ThreadInterface::GetVersion() reports >= 3; OStim 7.4c objects do
        // not have these vtable entries.
        virtual FurnitureType* getFurnitureType() = 0;
        // Cast the returned pointer to RE::TESObjectREFR*.
        virtual void* getFurnitureObject() = 0;
    };
}
