#pragma once

#include "Game/LocaleManager.h"
#include "Util/RNGUtil.h"

namespace PapyrusUtility {
	using VM = RE::BSScript::IVirtualMachine;

	std::string Translate(RE::StaticFunctionTag*, std::string text) {
        return LocaleManager::GetSingleton()->GetLocalization(text);
	}


	std::vector<RE::TESForm*> ShuffleFormArray(RE::StaticFunctionTag*, std::vector<RE::TESForm*> array) {
        std::shuffle(array.begin(), array.end(), RNGUtil::RNG);
        return array;
	}


	std::vector<RE::TESQuest*> GetQuestsWithGlobal(RE::StaticFunctionTag*, RE::TESGlobal* tag) {
        std::vector<RE::TESQuest*> quests;

		for (RE::TESQuest* quest : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESQuest>()) {
            if (quest->textGlobals && !quest->textGlobals->empty() && std::find(quest->textGlobals->begin(), quest->textGlobals->end(), tag) != quest->textGlobals->end()) {
                quests.push_back(quest);
			}
		}

		return quests;
	}

	bool Bind(VM* a_vm) {
		const auto obj = "OUtility"sv;

		BIND(Translate);

		BIND(ShuffleFormArray);

		BIND(GetQuestsWithGlobal);

		return true;
	}
}