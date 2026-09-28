#pragma once

#include "GameRecord.h"

namespace GameAPI {
    struct GameVoice : public GameRecord<RE::BGSVoiceType> {
        inline GameVoice() {}
        inline GameVoice(RE::BGSVoiceType* type) { form = type; }
    };
}