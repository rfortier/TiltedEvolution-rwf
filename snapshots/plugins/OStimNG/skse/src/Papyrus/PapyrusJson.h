#pragma once

#include "ScriptAPI/JsonScript.h"

namespace PapyrusJson {
    using VM = RE::BSScript::IVirtualMachine;

    std::vector<RE::Actor*> GetActors(RE::StaticFunctionTag*, std::string json) {
        std::vector<RE::Actor*> actors;
        for (GameAPI::GameActor actor : ScriptAPI::Json::getActors(json)) {
            actors.push_back(actor.form);
        }
        return actors;
    }

    std::string GetScene(RE::StaticFunctionTag*, std::string json) {
        return ScriptAPI::Json::getScene(json);
    }

    std::vector<std::string> GetMetadata(RE::StaticFunctionTag*, std::string json) {
        return ScriptAPI::Json::getMetadata(json);
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OJSON"sv;

        BIND(GetActors);
        BIND(GetScene);
        BIND(GetMetadata);

        return true;
    }
}