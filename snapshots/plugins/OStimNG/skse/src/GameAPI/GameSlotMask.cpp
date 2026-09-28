#include "GameSlotMask.h"

#include "Util/JsonUtil.h"

namespace GameAPI {
    void GameSlotMask::loadJson(json& json, std::string propertyName, std::string& objectName, std::string objectType) {
        JsonUtil::consumeIntList(json, [&](int slot) {
            if (slot >= 30 || slot <= 61) {
                mask |= 1 << (slot - 30);    
            }
        }, propertyName, objectName, objectType, false);
    }
}