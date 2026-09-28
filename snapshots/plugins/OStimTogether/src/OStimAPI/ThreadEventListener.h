#pragma once
#include "Thread.h"

namespace OStim
{
    class ThreadEventListener
    {
    public:
        // IMPORTANT: exact OStim ABI. Do NOT add a virtual destructor here.
        virtual void listen(Thread* thread) = 0;
    };
}
