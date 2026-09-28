#include "GamePosition.h"

#include "Util/JsonUtil.h"

namespace GameAPI {
    void GamePosition::loadJson(std::string& path, json json) {
        if (!json.is_object()) {
            logger::warn("position definition in file {} is malformed", path);
        }

        JsonUtil::loadFloat(json, x, "x", path, "position", false);
        JsonUtil::loadFloat(json, y, "y", path, "position", false);
        JsonUtil::loadFloat(json, z, "z", path, "position", false);
        JsonUtil::loadFloat(json, r, "r", path, "position", false);
    }
}