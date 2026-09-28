#include "SoundUtil.h"

namespace Sound {
    SoundSet* getSoundSet(std::vector<SoundSet>& sounds, GameAPI::GameActor actor, GameAPI::GameActor target) {
        for (SoundSet& sound : sounds) {
            if (!sound.condition || sound.condition.fulfills(actor, target)) {
                return &sound;
            }
        }

        return nullptr;
    }

    DialogueSet* getDialogueSet(std::vector<DialogueSet>& dialogues, GameAPI::GameActor actor, GameAPI::GameActor target) {
        for (DialogueSet& dialogue : dialogues) {
            if (!dialogue.condition || dialogue.condition.fulfills(actor, target)) {
                return &dialogue;
            }
        }

        return nullptr;
    }
}