#pragma once

#include "Graph/GraphTable.h"
#include "Util/FormUtil.h"
#include "Core/ThreadManager.h"
#include "Trait/TraitTable.h"

namespace PapyrusActor {
    using VM = RE::BSScript::IVirtualMachine;

    RE::TESNPC* GetLeveledActorBase(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID,
                                    RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) {
            a_vm->TraceStack("Actor is None", a_stackID);
            return nullptr;
        }

        return a_actor->GetActorBase();
    }

    int32_t GetSex(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                   RE::TESNPC* a_base) {
        if (!a_base) {
            a_vm->TraceStack("ActorBase is None", a_stackID);
            return -1;
        }

        auto sex = a_base->GetSex();
        switch (sex) {
            case (RE::SEX::kMale):
                return 0;
            case (RE::SEX::kFemale):
                return 1;
        }

        return -1;
    }

    RE::TESRace* GetRace(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                         RE::TESNPC* a_base) {
        if (!a_base) {
            a_vm->TraceStack("ActorBase is None", a_stackID);
            return nullptr;
        }

        return a_base->GetRace();
    }

    RE::BGSVoiceType* GetVoiceType(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                                   RE::TESNPC* a_base) {
        if (!a_base) {
            a_vm->TraceStack("ActorBase is None", a_stackID);
            return nullptr;
        }

        return a_base->GetVoiceType();
    }

    std::vector<RE::Actor*> GetActors(RE::StaticFunctionTag*, RE::TESObjectREFR* a_centerRef, float a_radius) {
        std::vector<RE::Actor*> actors;
        auto pl = RE::ProcessLists::GetSingleton();
        if (a_centerRef && a_radius > 0) {
            const auto originPos = a_centerRef->GetPosition();
            for (auto& handle : pl->highActorHandles) {
                auto actor = handle.get().get();
                if (actor) {
                    const auto actorPos = actor->GetPosition();
                    if (util::within_radius(originPos, actorPos, a_radius)) actors.push_back(actor);
                }
            }
        } else {
            for (auto& handle : pl->highActorHandles) {
                auto actor = handle.get().get();
                if (actor) {
                    actors.push_back(actor);
                }
            }
        }

        return actors;
    }

    void SetPositionEx(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                       RE::Actor* a_actor, float a_x, float a_y, float a_z) {
        if (!a_actor) {
            a_vm->TraceStack("Actor is None", a_stackID);
            return;
        }

        RE::NiPoint3 pos{a_x, a_y, a_z};
        a_actor->SetPosition(pos, false);
    }

    RE::Actor* GetActorFromBase(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                                RE::TESNPC* a_base) {
        if (!a_base) {
            a_vm->TraceStack("ActorBase is None", a_stackID);
            return nullptr;
        }

        const auto& [map, lock] = RE::TESForm::GetAllForms();
        const RE::BSReadWriteLock l{lock};
        for (auto& [id, form] : *map) {
            if (form->GetSavedFormType() == RE::FormType::ActorCharacter) {
                auto actor = form->As<RE::Actor>();
                if (actor && actor->GetActorBase() == a_base) {
                    return actor;
                }
            }
        }

        return nullptr;
    }

    std::vector<RE::TESNPC*> LookupRelationshipPartners(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID,
                                                        RE::StaticFunctionTag*, RE::Actor* a_actor,
                                                        RE::BGSAssociationType* a_type) {
        if (!a_actor) {
            a_vm->TraceStack("Actor is None", a_stackID);
            return {};
        }

        if (!a_type) {
            a_vm->TraceStack("AssociationType is None", a_stackID);
            return {};
        }

        std::vector<RE::TESNPC*> ret;
        auto base = a_actor->GetActorBase();
        for (auto& relationship : *base->relationships) {
            if (relationship->assocType == a_type) {
                if (relationship->npc1 == base) {
                    ret.push_back(relationship->npc2);
                } else if (relationship->npc2 == base) {
                    ret.push_back(relationship->npc1);
                }
            }
        }

        return ret;
    }

    void ToggleCombat(RE::StaticFunctionTag*, bool a_enable) {
        auto pl = RE::ProcessLists::GetSingleton();
        pl->runDetection = a_enable;
        pl->ClearCachedFactionFightReactions();
    }

    bool DetectionActive(RE::StaticFunctionTag*) {
        auto pl = RE::ProcessLists::GetSingleton();
        return pl->runDetection;
    }

    std::vector<RE::Actor*> SortActorsByDistance(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID,
                                                 RE::StaticFunctionTag*, RE::TESObjectREFR* a_centerRef,
                                                 std::vector<RE::Actor*> a_list) {
        if (!a_centerRef) {
            a_vm->TraceStack("CenterRef is None", a_stackID);
            return {};
        }

        const auto originPos = a_centerRef->GetPosition();
        std::sort(a_list.begin(), a_list.end(), [&](RE::Actor* a_refA, RE::Actor* a_refB) {
            return originPos.GetDistance(a_refA->GetPosition()) < originPos.GetDistance(a_refB->GetPosition());
        });

        return a_list;
    }

    std::vector<RE::Actor*> RemoveActorsWithGender(RE::StaticFunctionTag*, std::vector<RE::Actor*> a_actors,
                                                   int32_t a_gender) {
        std::vector<RE::Actor*> ret;

        auto sex = static_cast<RE::SEX>(a_gender);
        for (auto actor : a_actors) {
            auto base = actor->GetActorBase();
            if (base->GetSex() != sex) ret.push_back(actor);
        }

        return ret;
    }

    std::vector<RE::TESForm*> GetEquippedAmmo(RE::StaticFunctionTag*, RE::Actor* a_actor)  // modified from PO3
    {
        std::vector<RE::TESForm*> result;

        if (!a_actor) {
            return result;
        }

        auto inv = a_actor->GetInventory();
        for (const auto& [item, data] : inv) {
            if (item->Is(RE::FormType::LeveledItem)) {
                continue;
            }
            const auto& [count, entry] = data;
            if (count > 0 && entry->IsWorn() && item->Is(RE::FormType::Ammo)) {
                result.push_back(item);
            }
        }

        return result;
    }

    bool IsWig(RE::StaticFunctionTag*, RE::Actor* actor, RE::TESObjectARMO* armor) {
        return FormUtil::isWig(actor, armor);
    }

    void SetFaceLight(RE::StaticFunctionTag*, std::string stateVal, RE::Actor* actor, std::string light) {
        if (actor != nullptr) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (light == "") {
                if (threadActor->isObjectEquipped("light")) {
                    threadActor->unequipObject("light");
                } else {
                    threadActor->equipObject("light");
                }
            } else {
                Trait::TraitTable::setEquipObjectID(actor->GetFormID(), "light", light);       
                threadActor->refreshObject("light");
            }
        }        
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OSANative"sv;

        BIND(GetEquippedAmmo, true);
        BIND(RemoveActorsWithGender, true);
        BIND(SortActorsByDistance, true);
        BIND(GetActorFromBase, true);
        BIND(GetLeveledActorBase, true);
        BIND(GetSex, true);
        BIND(GetRace, true);
        BIND(GetVoiceType, true);
        BIND(GetActors);
        BIND(SetPositionEx);
        BIND(LookupRelationshipPartners, true);
        BIND(ToggleCombat);
        BIND(DetectionActive);
        BIND(IsWig);
        BIND(SetFaceLight);

        return true;
    }
}  // namespace PapyrusActor