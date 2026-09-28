#pragma once

#include "GameAPI/GameCondition.h"
#include "GameAPI/GameSound.h"

namespace Sound {
    struct SoundSet {
        GameAPI::GameCondition condition;
        GameAPI::GameSound sound;
        std::string expression = "";
        int moanIntervalOverride = 0;
    };
}