#pragma once

#include "GameAPI/GameActor.h"

namespace ScriptAPI {
    namespace Library {
        std::vector<std::string> getAllNodes();
        std::vector<std::string> getNodesInRange(std::string id, std::vector<GameAPI::GameActor> actors, int distance);
    }
}