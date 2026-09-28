#pragma once

#include "GameAPI/GameActor.h"

namespace ScriptAPI {
    namespace ActionData {
        bool fulfillsActorConditions(std::string actionType, GameAPI::GameActor actor);
        bool fulfillsTargetConditions(std::string actionType, GameAPI::GameActor actor);
        bool fulfillsPerformerConditions(std::string actionType, GameAPI::GameActor actor);
    }
}