#pragma once

#include "ThreadStartParams.h"

namespace Threading {
    void startPlayerThread(ThreadStartParams params);

    bool getInDialogue(std::vector<GameAPI::GameActor> actors);
    void handleFurniture(ThreadStartParams params);
    void addFurniture(ThreadStartParams params, GameAPI::GameObject furniture);
    void handleActorAdding(ThreadStartParams params);
    void handleActorAddingInner(ThreadStartParams params);
    void addActor(ThreadStartParams params, GameAPI::GameActor actor);
    void handleActorSorting(ThreadStartParams params);
    void sortActors(ThreadStartParams params, int playerIndex);
    void handleStartingNode(ThreadStartParams params);
    void startInner(ThreadStartParams params);
}