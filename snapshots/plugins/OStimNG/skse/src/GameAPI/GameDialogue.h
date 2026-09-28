#pragma once

#include "GameActor.h"
#include "GameRecord.h"
#include "GameVoice.h"

namespace GameAPI {
    struct GameDialogue : public GameRecord<RE::TESTopic> {
    public:
        inline GameDialogue() {}
        inline GameDialogue(RE::TESTopic* topic) { form = topic; }

        void sayTo(GameActor speaker, GameActor target) const;
        void sayAs(GameActor speaker, GameActor target, GameVoice voice) const;

    private:
        void say(RE::Actor* actor, RE::TESTopicInfo* info, RE::BGSVoiceType* voice) const;
    };
}