#pragma once

#include "ThreadStartParams.h"

namespace Threading {
    void startDebugNPCThread(ThreadStartParams params);

    namespace {
        void handleActorAddingDebug(ThreadStartParams params);
        void addActorDebug(ThreadStartParams params, GameAPI::GameActor actor);
    }
}