#pragma once
#include <cstdint>

namespace OStim
{
    class ThreadActor
    {
    public:
        virtual void* getGameActor() = 0;

        virtual void undress() = 0;
        virtual void undressPartial(std::uint32_t slotmask) = 0;
        virtual void removeWeapons() = 0;
        virtual void redress() = 0;
        virtual void redressPartial(std::uint32_t slotmask) = 0;
        virtual void addWeapons() = 0;
    };
}
