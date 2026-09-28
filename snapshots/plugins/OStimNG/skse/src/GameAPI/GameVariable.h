#pragma once

#include "GameRecord.h"

namespace GameAPI {
    struct GameVariable : public GameRecord<RE::TESGlobal>{
    public:
        inline GameVariable() {}
        inline GameVariable(RE::TESGlobal* global) { form = global; }

        inline float getValue() {
            return form->value;
        }

        inline void setValue(float value) {
            form->value = value;
        }
    };
}