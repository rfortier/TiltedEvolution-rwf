#pragma once

#include "GameAPI/GameArmor.h"

namespace Trait {
    struct EquipObject {
        std::string name;
        GameAPI::GameArmor item;
        std::unordered_map<std::string, GameAPI::GameArmor> variants;
    };
    
}