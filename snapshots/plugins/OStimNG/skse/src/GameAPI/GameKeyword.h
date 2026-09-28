#pragma once

#include "GameRecord.h"

namespace GameAPI {
    struct GameKeyword : public GameRecord<RE::BGSKeyword> {
        inline GameKeyword() {}
        inline GameKeyword(RE::BGSKeyword* keyword) { form = keyword; }
    };
}