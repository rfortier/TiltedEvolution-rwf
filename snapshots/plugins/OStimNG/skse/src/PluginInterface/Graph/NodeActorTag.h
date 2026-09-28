#pragma once

namespace OStim {
    class NodeActorTag {
    public:
        // --- ABI version 1 ---
        virtual const char* getTagID() = 0;
    };
}