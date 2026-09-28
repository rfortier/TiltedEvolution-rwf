#pragma once

#include "GameRecord.h"

namespace GameAPI {
    struct GameItem : public GameRecord<RE::TESBoundObject> {
        inline GameItem() {}
        inline GameItem(RE::TESBoundObject* item) { form = item; }
    };
}