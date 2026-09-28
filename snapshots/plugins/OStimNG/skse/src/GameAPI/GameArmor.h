#pragma once

#include "GameItem.h"
#include "GameKeyword.h"
#include "GameRecord.h"
#include "GameSlotMask.h"

namespace GameAPI {
    struct GameArmor : public GameRecord<RE::TESObjectARMO> {
        static void forAll(std::function<void(GameArmor)> consumer);

        inline GameArmor() {}
        inline GameArmor(RE::TESObjectARMO* armor) { form = armor; }

        inline GameItem toItem() { return form; }

        inline GameSlotMask getSlotMask() { return form->GetSlotMask().underlying(); }

        inline bool hasKeyword(GameKeyword keyword) const { return form->HasKeyword(keyword.form); }
        inline void addKeyword(GameKeyword keyword) const { form->AddKeyword(keyword.form); }
        std::vector<GameKeyword> getKeywords();
    };
}