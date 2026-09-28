#pragma once

#include "ActorProperties/ActorPropertyTable.h"
#include "GameAPI/GameCondition.h"
#include "GameAPI/GameUtil.h"
#include "Util/ActorUtil.h"
#include "Util/CompatibilityTable.h"
#include "Util/VectorUtil.h"

namespace PapyrusActorUtil {
    using VM = RE::BSScript::IVirtualMachine;

    bool HasSchlong(RE::StaticFunctionTag*, RE::Actor* actor) {
        return Compatibility::CompatibilityTable::hasSchlong(actor);
    }

    bool FulfillsCondition(RE::StaticFunctionTag*, RE::Actor* actor, RE::BGSPerk* condition) {
        return GameAPI::GameCondition{condition}.fulfills(actor);
    }

    bool FulfillsAnyCondition(RE::StaticFunctionTag*, RE::Actor* actor, std::vector<RE::BGSPerk*> conditions) {
        for (RE::BGSPerk* condition : conditions) {
            if (GameAPI::GameCondition{condition}.fulfills(actor)) {
                return true;
            }
        }
        return false;
    }

    bool FulfillsAllConditions(RE::StaticFunctionTag*, RE::Actor* actor, std::vector<RE::BGSPerk*> conditions) {
        for (RE::BGSPerk* condition : conditions) {
            if (!GameAPI::GameCondition{condition}.fulfills(actor)) {
                return false;
            }
        }
        return true;
    }

    void SayTo(RE::StaticFunctionTag*, RE::Actor* actor, RE::Actor* target, RE::TESTopic* dialogue) {
        GameAPI::GameDialogue(dialogue).sayTo(actor, target);
    }

    void SayAs(RE::StaticFunctionTag*, RE::Actor* actor, RE::Actor* target, RE::TESTopic* dialogue, RE::BGSVoiceType* voice) {
        GameAPI::GameDialogue(dialogue).sayAs(actor, target, voice);
    }

    std::vector<RE::Actor*> EmptyArray(RE::StaticFunctionTag*) {
        return {};
    }

    std::vector<RE::Actor*> CreateArray(RE::StaticFunctionTag*, int size, RE::Actor* filler) {
        if (size <= 0) {
            return std::vector<RE::Actor*>();
        }

        std::vector<RE::Actor*> actors = std::vector<RE::Actor*>(size);
        for (int i = 0; i < size; i++) {
            actors[i] = filler;
        }

        return actors;
    }

    std::vector<RE::Actor*> ToArray(RE::StaticFunctionTag*, RE::Actor* one, RE::Actor* two, RE::Actor* three, RE::Actor* four, RE::Actor* five, RE::Actor* six, RE::Actor* seven, RE::Actor* eight, RE::Actor* nine, RE::Actor* ten) {
        std::vector<RE::Actor*> actors;

        if (one) actors.push_back(one);
        if (two) actors.push_back(two);
        if (three) actors.push_back(three);
        if (four) actors.push_back(four);
        if (five) actors.push_back(five);
        if (six) actors.push_back(six);
        if (seven) actors.push_back(seven);
        if (eight) actors.push_back(eight);
        if (nine) actors.push_back(nine);
        if (ten) actors.push_back(ten);

        return actors;
    }

    std::vector<RE::Actor*> Sort(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<RE::Actor*> dominantActors, int playerIndex) {
        std::vector<GameAPI::GameActor> gameActors = GameAPI::GameActor::convertVector(actors);
        
        std::vector<GameAPI::GameActor> gameDoms = GameAPI::GameActor::convertVector(dominantActors);

        ActorUtil::sort(gameActors, gameDoms, playerIndex);

        std::vector<RE::Actor*> ret;
        for (GameAPI::GameActor actor : gameActors) {
            ret.push_back(actor.form);
        }

        return ret;
    }

    std::vector<RE::Actor*> GetActorsInRangeV2(RE::StaticFunctionTag*, RE::TESObjectREFR* center, float range, bool includeCenter, bool includePlayer, bool ostimActorsOnly, RE::BGSPerk* condition) {
        std::vector<RE::Actor*> actors;

        GameAPI::GameUtil::ForEachReferenceInRange(center, range, [&actors, center, includeCenter, includePlayer, ostimActorsOnly, condition](RE::TESObjectREFR* ref) {
            if (!ref->Is(RE::Actor::FORMTYPE)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            RE::Actor* actor = ref->As<RE::Actor>();

            if (!includeCenter && actor == center) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (!includePlayer && actor->IsPlayerRef()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (actor->IsDisabled() || actor->IsGhost()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (condition && !condition->perkConditions.IsTrue(actor, actor)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (ostimActorsOnly && ActorProperties::ActorPropertyTable::getActorType(actor) == "") {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            actors.push_back(actor);

            return RE::BSContainer::ForEachResult::kContinue;
        });

        RE::NiPoint3 center2 = center->GetPosition();
        std::sort(actors.begin(), actors.end(), [&](RE::Actor* actorA, RE::Actor* actorB) {
            return actorA->GetPosition().GetSquaredDistance(center2) <
                   actorB->GetPosition().GetSquaredDistance(center2);
        });

        return actors;
    }

    std::vector<std::string> ActorsToNames(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors) {
        std::vector<std::string> names;
        for (RE::Actor* actor : actors) {
            names.push_back(actor->GetDisplayFullName());
        }
        return names;
    }


    bool Bind(VM* a_vm) {
        const auto obj = "OActorUtil"sv;

        BIND(HasSchlong);
        BIND(FulfillsCondition);
        BIND(FulfillsAnyCondition);
        BIND(FulfillsAllConditions);

        BIND(SayTo);
        BIND(SayAs);

        BIND(EmptyArray);
        BIND(CreateArray);
        BIND(ToArray);

        BIND(Sort);
        BIND(GetActorsInRangeV2);
        BIND(ActorsToNames);

        return true;
    }
}