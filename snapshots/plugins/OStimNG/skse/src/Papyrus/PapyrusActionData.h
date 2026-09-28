#pragma once

#include "ScriptAPI/ActionDataScript.h"

namespace PapyrusActionData {
    using VM = RE::BSScript::IVirtualMachine;

    bool FulfillsActorConditions(RE::StaticFunctionTag*, std::string actionType, RE::Actor* actor) {
        return ScriptAPI::ActionData::fulfillsActorConditions(actionType, actor);
    }

    bool FulfillsTargetConditions(RE::StaticFunctionTag*, std::string actionType, RE::Actor* actor) {
        return ScriptAPI::ActionData::fulfillsTargetConditions(actionType, actor);
    }

    bool FulfillsPerformerConditions(RE::StaticFunctionTag*, std::string actionType, RE::Actor* actor) {
        return ScriptAPI::ActionData::fulfillsPerformerConditions(actionType, actor);
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OActionData"sv;

        BIND(FulfillsActorConditions);
        BIND(FulfillsTargetConditions);
        BIND(FulfillsPerformerConditions);

        return true;
    }
}