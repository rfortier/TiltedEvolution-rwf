#pragma once

#include "SoundUtil.h"

namespace Sound {
    struct ReactionSet {
        std::vector<SoundSet> sound;
        std::vector<SoundSet> soundMuffled;
        std::vector<DialogueSet> dialogue;

        inline SoundSet* getSound(GameAPI::GameActor actor, GameAPI::GameActor target) { return getSoundSet(sound, actor, target); }
        inline SoundSet* getSoundMuffled(GameAPI::GameActor actor, GameAPI::GameActor target) { return getSoundSet(soundMuffled, actor, target); }
        inline DialogueSet* getDialogue(GameAPI::GameActor actor, GameAPI::GameActor target) { return getDialogueSet(dialogue, actor, target); }

        inline bool empty() { return sound.empty() && soundMuffled.empty() && dialogue.empty(); }
    };
}