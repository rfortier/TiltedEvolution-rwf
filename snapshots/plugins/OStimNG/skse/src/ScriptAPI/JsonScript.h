#pragma once

#include "GameAPI/GameActor.h"

namespace ScriptAPI {
    namespace Json {
        std::vector<GameAPI::GameActor> getActors(std::string json);
        std::string getScene(std::string json);
        std::vector<std::string> getMetadata(std::string json);
    }
}