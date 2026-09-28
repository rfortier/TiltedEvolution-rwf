#pragma once

#include "Core/ThreadManager.h"
#include "Furniture/Furniture.h"
#include "Furniture/FurnitureTable.h"

namespace PapyrusFurniture {
    using VM = RE::BSScript::IVirtualMachine;

    std::string GetFurnitureType(RE::StaticFunctionTag*, RE::TESObjectREFR* furnitureRef) {
        return Furniture::FurnitureTable::getFurnitureType(furnitureRef, false)->id;
    }

    bool IsChildOf(RE::StaticFunctionTag*, std::string superType, std::string subType) {
        Furniture::FurnitureType* super = Furniture::FurnitureTable::getFurnitureType(superType);
        Furniture::FurnitureType* sub = Furniture::FurnitureTable::getFurnitureType(subType);

        return super && sub && sub->isChildOf(super);
    }

    std::vector<RE::TESObjectREFR*> FindFurniture(RE::StaticFunctionTag*, int actorCount, RE::TESObjectREFR* centerRef, float radius, float sameFloor) {
        if (!centerRef) {
            return {};
        }
        
        std::vector<std::pair<Furniture::FurnitureType*, GameAPI::GameObject>> furniture = Furniture::findFurniture(actorCount, {centerRef->GetPosition(), 0}, radius, sameFloor);
        std::vector<RE::TESObjectREFR*> ret;
        for (auto& [type, object] : furniture) {
            ret.push_back(object.form);
        }
        return ret;
    }

    RE::TESObjectREFR* FindFurnitureOfType(RE::StaticFunctionTag*, std::string type, RE::TESObjectREFR* centerRef, float radius, float sameFloor) {
        return Furniture::findFurniture(Furniture::FurnitureTable::getFurnitureType(type), centerRef, radius, sameFloor).form;
    }

    std::vector<float> GetOffset(RE::StaticFunctionTag*, RE::TESObjectREFR* furnitureRef) {
        Furniture::FurnitureOffset offset = Furniture::getOffset(furnitureRef);
        return {offset.x, offset.y, offset.z, offset.rotation, offset.scale};
    }

    int GetSceneID(RE::StaticFunctionTag*, RE::TESObjectREFR* furnitureRef) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->findThread((GameAPI::GameObject)furnitureRef);
        return thread ? thread->m_threadId : -1;
    }

    void ResetClutter(RE::StaticFunctionTag*, RE::TESObjectREFR* centerRef, float radius) {
        Furniture::resetClutter(centerRef, radius);
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OFurniture"sv;

        BIND(GetFurnitureType);
        BIND(IsChildOf);
        BIND(FindFurniture);
        BIND(FindFurnitureOfType);
        BIND(GetOffset);
        BIND(GetSceneID);
        BIND(ResetClutter);

        return true;
    }
}