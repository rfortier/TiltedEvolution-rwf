#include "GameArmor.h"

namespace GameAPI {
    void GameArmor::forAll(std::function<void(GameArmor)> consumer) {
        for (RE::TESObjectARMO* armor : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESObjectARMO>()) {
            consumer(armor);
        }
    }

    std::vector<GameKeyword> GameArmor::getKeywords() {
        std::vector<GameKeyword> keywords;
        for (RE::BGSKeyword* keyword : form->GetKeywords()) {
            keywords.push_back(keyword);
        }
        return keywords;
    }
}