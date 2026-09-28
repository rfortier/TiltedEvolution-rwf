#pragma once

#include "ThreadStartParams.h"

#include "Util/IDMap.h"

namespace Threading {
    class ThreadBuilder {
    public:
        inline static int add(ThreadStartParams params) { return paramMap.put(params); }
        inline static ThreadStartParams* get(int builderID) { return paramMap.get(builderID); }
        inline static void remove(int builderID) { paramMap.remove(builderID); }
        inline static void reset() { paramMap.reset(); }

    private:
        inline static Util::IDMap<ThreadStartParams> paramMap;
    };
}