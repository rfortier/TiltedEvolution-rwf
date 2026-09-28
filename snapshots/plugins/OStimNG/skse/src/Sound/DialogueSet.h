#pragma once

#include "GameAPI/GameCondition.h"
#include "GameAPI/GameDialogue.h"

namespace Sound {
    struct DialogueSet {
        GameAPI::GameCondition condition;
        GameAPI::GameDialogue dialogue;
        std::string expression = "";
        int dialogueThreshold = 0;
        int moanIntervalOverride = 0;
    };
}