#pragma once

#include "Thread.h"

namespace OStim {
    class ThreadEventListener {
    public:
        // --- ABI version 1 ---
        virtual void listen(Thread* thread) = 0;
    };
}