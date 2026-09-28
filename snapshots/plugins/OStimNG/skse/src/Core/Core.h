#pragma once

#include "GameAPI/GameActor.h"

namespace Threading {
    void freeActor(GameAPI::GameActor actor, bool byGameLoad);
    bool isEligible(GameAPI::GameActor actor);
}