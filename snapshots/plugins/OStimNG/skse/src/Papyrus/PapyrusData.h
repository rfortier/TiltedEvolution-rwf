#pragma once

#include "Events/EventListener.h"
#include "Game/LocaleManager.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"
#include "Serial/Manager.h"
#include "Sound/SoundTable.h"
#include "Trait/TraitTable.h"
#include "Util/MathUtil.h"
#include "Util/StringUtil.h"

namespace PapyrusData {
	using VM = RE::BSScript::IVirtualMachine;


	uint32_t GetUndressingSlotMask(RE::StaticFunctionTag*) {
		return MCM::MCMTable::getUndressingMask();
	}

	void SetUndressingSlotMask(RE::StaticFunctionTag*, uint32_t mask) {
		MCM::MCMTable::setUndressingMask(mask); }


    std::vector<std::string> PairsToNames(RE::StaticFunctionTag*, std::vector<std::string> pairs) {
        std::vector<std::string> names;
        for (int i = 1; i < pairs.size(); i += 2) {
            names.push_back(pairs[i]);
        }
        return names;
    }


#pragma region equip_objects
	std::vector<std::string> GetEquipObjectTypes(RE::StaticFunctionTag*) {
        std::vector<std::string> types = Trait::TraitTable::getEquipObjectTypes();
        StringUtil::sort(types);
        return types;
	}

	std::vector<std::string> GetEquipObjectPairs(RE::StaticFunctionTag*, int formID, std::string type) {
        StringUtil::toLower(&type);
        return Trait::TraitTable::getEquipObjectPairs(formID, type);
	}

	std::string GetEquipObjectName(RE::StaticFunctionTag*, int formID, std::string type) {
        StringUtil::toLower(&type);
        return Trait::TraitTable::getEquipObjectName(formID, type);
	}

	void SetEquipObjectID(RE::StaticFunctionTag*, int formID, std::string type, std::string id) {
        Trait::TraitTable::setEquipObjectID(formID, type, id);
	}
#pragma endregion

#pragma region voice
	std::vector<std::string> GetVoiceSetPairs(RE::StaticFunctionTag*) {
		return Sound::SoundTable::getVoiceSetPairs();
	}

	std::string GetVoiceSetName(RE::StaticFunctionTag*, int formID) {
        return Sound::SoundTable::getVoiceSetName(GameAPI::GameRecordIdentifier(MathUtil::intToUint(formID)));
	}

	void SetVoiceSet(RE::StaticFunctionTag*, int formID, std::string voice) {
        Sound::SoundTable::setVoiceSet(GameAPI::GameRecordIdentifier(MathUtil::intToUint(formID)), voice);
	}
#pragma endregion

#pragma region preferences
#pragma region actions
    std::vector<std::string> GetActions(RE::StaticFunctionTag*) {
        std::vector<std::string> actions = Graph::GraphTable::getActions();
        StringUtil::sort(actions);
        return actions;
    }

	float GetActionStimulation(RE::StaticFunctionTag*, int role, int formID, std::string action) {
        float stimulation = Serialization::getActionStimulation(static_cast<Graph::Role>(role), GameAPI::GameRecordIdentifier{MathUtil::intToUint(formID)}, action);
        if (!std::isnan(stimulation)) {
            return stimulation;
		}

        Graph::Action::ActionAttributes* attributes = Graph::GraphTable::getActionAttributesByType(action);
        if (!attributes) {
            return 0.0f;
		}

		return attributes->roles.get(static_cast<Graph::Role>(role))->stimulation;
    }

    void SetActionStimulation(RE::StaticFunctionTag*, int role, int formID, std::string action, float stimulation) {
        Serialization::setActionStimulation(static_cast<Graph::Role>(role), formID, action, stimulation);
    }

    float GetActionMaxStimulation(RE::StaticFunctionTag*, int role, int formID, std::string action) {
        float stimulation = Serialization::getActionMaxStimulation(static_cast<Graph::Role>(role), GameAPI::GameRecordIdentifier{MathUtil::intToUint(formID)}, action);
        if (!std::isnan(stimulation)) {
            return stimulation;
        }

        Graph::Action::ActionAttributes* attributes = Graph::GraphTable::getActionAttributesByType(action);
        if (!attributes) {
            return 0.0f;
        }

        return attributes->roles.get(static_cast<Graph::Role>(role))->maxStimulation;
    }

    void SetActionMaxStimulation(RE::StaticFunctionTag*, int role, int formID, std::string action, float stimulation) {
        Serialization::setActionMaxStimulation(static_cast<Graph::Role>(role), formID, action, stimulation);
    }

    float GetActionDefaultStimulation(RE::StaticFunctionTag*, int role, std::string action) {
        Graph::Action::ActionAttributes* attributes = Graph::GraphTable::getActionAttributesByType(action);
        if (!attributes) {
            return 0.0f;
        }

        return attributes->roles.get(static_cast<Graph::Role>(role))->stimulation;
    }

    void ResetActionStimulation(RE::StaticFunctionTag*, int role, int formID, std::string action) {
        Serialization::unsetActionStimulation(static_cast<Graph::Role>(role), GameAPI::GameRecordIdentifier{MathUtil::intToUint(formID)}, action);
    }

    float GetActionDefaultMaxStimulation(RE::StaticFunctionTag*, int role, std::string action) {
        Graph::Action::ActionAttributes* attributes = Graph::GraphTable::getActionAttributesByType(action);
        if (!attributes) {
            return 0.0f;
        }

        return attributes->roles.get(static_cast<Graph::Role>(role))->maxStimulation;
    }

    void ResetActionMaxStimulation(RE::StaticFunctionTag*, int role, int formID, std::string action) {
        Serialization::unsetActionMaxStimulation(static_cast<Graph::Role>(role), GameAPI::GameRecordIdentifier{MathUtil::intToUint(formID)}, action);
    }
#pragma endregion

#pragma region events
    std::vector<std::string> GetEvents(RE::StaticFunctionTag*) {
        std::vector<std::string> events = Graph::GraphTable::getEvents();
        StringUtil::sort(events);
        return events;
    }

    float GetEventStimulation(RE::StaticFunctionTag*, int role, int formID, std::string evt) {
        float stimulation = Serialization::getEventStimulation(static_cast<Graph::Role>(role), formID, evt);
        if (!std::isnan(stimulation)) {
            return stimulation;
        }

        Graph::Event* graphEvent = Graph::GraphTable::getEvent(evt);
        if (!graphEvent) {
            return 0.0f;
        }

        return graphEvent->roles.get(static_cast<Graph::Role>(role))->stimulation;
    }

    void SetEventStimulation(RE::StaticFunctionTag*, int role, int formID, std::string evt, float stimulation) {
        Serialization::setEventStimulation(static_cast<Graph::Role>(role), formID, evt, stimulation);
    }

    float GetEventMaxStimulation(RE::StaticFunctionTag*, int role, int formID, std::string evt) {
        float stimulation = Serialization::getEventMaxStimulation(static_cast<Graph::Role>(role), formID, evt);
        if (!std::isnan(stimulation)) {
            return stimulation;
        }

        Graph::Event* graphEvent = Graph::GraphTable::getEvent(evt);
        if (!graphEvent) {
            return 0.0f;
        }

        return graphEvent->roles.get(static_cast<Graph::Role>(role))->maxStimulation;
    }

    void SetEventMaxStimulation(RE::StaticFunctionTag*, int role, int formID, std::string evt, float stimulation) {
        Serialization::setEventMaxStimulation(static_cast<Graph::Role>(role), formID, evt, stimulation);
    }

    float GetEventDefaultStimulation(RE::StaticFunctionTag*, int role, std::string evt) {
        Graph::Event* graphEvent = Graph::GraphTable::getEvent(evt);
        if (!graphEvent) {
            return 0.0f;
        }

        return graphEvent->roles.get(static_cast<Graph::Role>(role))->stimulation;
    }

    void ResetEventStimulation(RE::StaticFunctionTag*, int role, int formID, std::string evt) {
        Serialization::unsetEventStimulation(static_cast<Graph::Role>(role), GameAPI::GameRecordIdentifier{MathUtil::intToUint(formID)}, evt);
    }

    float GetEventDefaultMaxStimulation(RE::StaticFunctionTag*, int role, std::string evt) {
        Graph::Event* graphEvent = Graph::GraphTable::getEvent(evt);
        if (!graphEvent) {
            return 0.0f;
        }

        return graphEvent->roles.get(static_cast<Graph::Role>(role))->maxStimulation;
    }

    void ResetEventMaxStimulation(RE::StaticFunctionTag*, int role, int formID, std::string evt) {
        Serialization::unsetEventMaxStimulation(static_cast<Graph::Role>(role), GameAPI::GameRecordIdentifier{MathUtil::intToUint(formID)}, evt);
    }
#pragma endregion
#pragma endregion

	void ResetSettings(RE::StaticFunctionTag*) {
		MCM::MCMTable::restoreDefaults();
	}

	void ExportSettings(RE::StaticFunctionTag*) {
		MCM::MCMTable::exportSettings();
	}

	void ImportSettings(RE::StaticFunctionTag*) {
		MCM::MCMTable::importSettings();
	}


    std::string ToLocalizedString(RE::StaticFunctionTag*, std::string text) {
        StringUtil::toLower(&text);
        return "$" + text;
    }

    std::string Localize(RE::StaticFunctionTag*, std::string text) {
        StringUtil::toLower(&text);
        return LocaleManager::GetSingleton()->GetLocalization(text);
    }

    void ReloadScene(RE::StaticFunctionTag*, std::string sceneId) {
        Graph::GraphTable::reloadNode(sceneId);
    }


	bool Bind(VM* a_vm) {
        const auto obj = "OData"sv;

		BIND(GetUndressingSlotMask);
        BIND(SetUndressingSlotMask);

        BIND(PairsToNames);

		BIND(GetEquipObjectTypes);
		BIND(GetEquipObjectPairs);
        BIND(GetEquipObjectName);
        BIND(SetEquipObjectID);

		BIND(GetVoiceSetPairs);
        BIND(GetVoiceSetName);
        BIND(SetVoiceSet);

        BIND(GetActions);

        BIND(GetActionStimulation);
        BIND(SetActionStimulation);
        BIND(GetActionMaxStimulation);
        BIND(SetActionMaxStimulation);
        BIND(GetActionDefaultStimulation);
        BIND(ResetActionStimulation);
        BIND(GetActionDefaultMaxStimulation);
        BIND(ResetActionMaxStimulation);

        BIND(GetEvents);

        BIND(GetEventStimulation);
        BIND(SetEventStimulation);
        BIND(GetEventMaxStimulation);
        BIND(SetEventMaxStimulation);
        BIND(GetEventDefaultStimulation);
        BIND(ResetEventStimulation);
        BIND(GetEventDefaultMaxStimulation);
        BIND(ResetEventMaxStimulation);

		BIND(ResetSettings);
        BIND(ExportSettings);
        BIND(ImportSettings);

        BIND(ToLocalizedString);
        BIND(Localize);

        BIND(ReloadScene);

        return true;
	}
}