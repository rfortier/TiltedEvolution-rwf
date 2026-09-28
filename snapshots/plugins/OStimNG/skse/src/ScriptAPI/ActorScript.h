#pragma once

#include "GameAPI/GameActor.h"

namespace ScriptAPI {
    namespace Actor {
        void setExpressionsEnabled(GameAPI::GameActor actor, bool enabled, bool allowOverride);

        bool hasMetadata(GameAPI::GameActor actor, std::string metadata);
        void addMetadata(GameAPI::GameActor actor, std::string metadata);
        std::vector<std::string> getMetadata(GameAPI::GameActor actor);

        bool hasMetaFloat(GameAPI::GameActor actor, std::string key);
        float getMetaFloat(GameAPI::GameActor actor, std::string key);
        void setMetaFloat(GameAPI::GameActor actor, std::string key, float value);

        bool hasMetaString(GameAPI::GameActor actor, std::string key);
        std::string getMetaString(GameAPI::GameActor actor, std::string key);
        void setMetaString(GameAPI::GameActor actor, std::string key, std::string value);
    }
}