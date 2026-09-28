#pragma once

#include "Util/FormUtil.h"

namespace PapyrusUndress {
    using VM = RE::BSScript::IVirtualMachine;

    bool CanUndress(RE::StaticFunctionTag*, RE::TESForm* item) {
        return FormUtil::canUndress(item);
    }

    bool IsWig(RE::StaticFunctionTag*, RE::Actor* actor, RE::TESObjectARMO* item) {
        return FormUtil::isWig(actor, item);
    }

    std::vector<RE::TESObjectARMO*> GetWornItems(RE::StaticFunctionTag*, RE::Actor* actor) {
        std::vector<RE::TESObjectARMO*> items;

        auto inventory = actor->GetInventory();
        for (const auto& [obj, data] : inventory) {
            auto& [count, entry] = data;

            if (entry->IsWorn() && obj->IsArmor()) {
                items.push_back(obj->As<RE::TESObjectARMO>());
            }
        }

        return items;
    }

    std::vector<RE::TESObjectARMO*> TrimArmorArray(RE::StaticFunctionTag*, std::vector<RE::TESObjectARMO*> items) {
        std::erase_if(items, [&](RE::TESObjectARMO* item) { return !item; });
        return items;
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OUndress"sv;

        BIND(CanUndress);
        BIND(IsWig);
        BIND(GetWornItems);
        BIND(TrimArmorArray);

        return true;
    }
}  // namespace PapyrusThread