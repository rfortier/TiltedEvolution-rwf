#pragma once

#include "ReactionSet.h"
#include "SoundUtil.h"

#include "GameAPI/GameSound.h"
#include "GameAPI/GameVoice.h"

namespace Sound {
    struct VoiceSet {
    public:
        std::string name = "";

        GameAPI::GameVoice voice;

        ReactionSet moan;

        ReactionSet climax;
        ReactionSet climaxCommentSelf;
        ReactionSet climaxCommentOther;

        std::unordered_map<std::string, ReactionSet> eventActorReactions;
        std::unordered_map<std::string, ReactionSet> eventTargetReactions;
        std::unordered_map<std::string, ReactionSet> eventPerformerReactions;

        GameAPI::GameDialogue postSceneDialogue;
    };
}