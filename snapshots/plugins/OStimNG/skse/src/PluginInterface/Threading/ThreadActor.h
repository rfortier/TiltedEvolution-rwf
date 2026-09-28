#pragma once

namespace OStim {
    class ThreadActor {
    public:
        // --- ABI version 1 ---
        // cast this to RE::Actor*
        virtual void* getGameActor() = 0;

        // --- ABI version 3 ---
        virtual void undress() = 0;
        virtual void undressPartial(uint32_t slotmask) = 0;
        virtual void removeWeapons() = 0;
        virtual void redress() = 0;
        virtual void redressPartial(uint32_t slotmask) = 0;
        virtual void addWeapons() = 0;
    };
}