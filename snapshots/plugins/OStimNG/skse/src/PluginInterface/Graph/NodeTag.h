#pragma once

namespace OStim {
    class NodeTag {
    public:
        // --- ABI version 1 ---
        virtual const char* getTagID() = 0;
    };
}