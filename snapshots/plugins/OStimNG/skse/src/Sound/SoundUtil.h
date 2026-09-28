#pragma once

#include "DialogueSet.h"
#include "SoundSet.h"

#include "GameAPI/GameSound.h"

namespace Sound {
    SoundSet* getSoundSet(std::vector<SoundSet>& sounds, GameAPI::GameActor actor, GameAPI::GameActor target);
    DialogueSet* getDialogueSet(std::vector<DialogueSet>& dialogues, GameAPI::GameActor actor, GameAPI::GameActor target);
}