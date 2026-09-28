#pragma once

#include "Furniture/FurnitureTable.h"
#include "Graph/GraphTable.h"
#include "Trait/Condition.h"
#include "Util/StringUtil.h"
#include "Util/VectorUtil.h"

namespace PapyrusSequence {
    using VM = RE::BSScript::IVirtualMachine;

    std::string randomSequence(std::vector<RE::Actor*> actors, std::string furnitureType, std::function<bool(Graph::Sequence*)> condition) {
        if (Graph::Sequence* sequence = Graph::GraphTable::getRandomSequence(Furniture::FurnitureTable::getFurnitureType(furnitureType), Trait::ActorCondition::create(GameAPI::GameActor::convertVector(actors)), condition)) {
            return sequence->id;
        }

        return "";
    }

    std::string randomSequence(std::vector<RE::Actor*> actors, std::function<bool(Graph::Sequence*)> condition) {
        return randomSequence(actors, "", condition);
    }

    // *********************************************************
    // start of papyrus bound functions
    // *********************************************************

    std::string GetRandomSequence(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors) {
        return randomSequence(actors, [](Graph::Sequence*) { return true; });
    }

    std::string GetRandomFurnitureSequence(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType) {
        return randomSequence(actors, furnitureType, [](Graph::Sequence*) { return true; });
    }


    std::string GetRandomSequenceWithSequenceTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string tag) {
        StringUtil::toLower(&tag);
        return randomSequence(actors, [&tag](Graph::Sequence* sequence) { return VectorUtil::contains(sequence->tags, tag); });
    }

    std::string GetRandomSequenceWithAnySequenceTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomSequence(actors, [&tags](Graph::Sequence* sequence) { return VectorUtil::containsAny(sequence->tags, tags); });
    }

    std::string GetRandomSequenceWithAnySequenceTagCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string tags) {
        return GetRandomSequenceWithAnySequenceTag(sft, actors, StringUtil::toTagVector(tags));
    }

    std::string GetRandomSequenceWithAllSequenceTags(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomSequence(actors, [&tags](Graph::Sequence* sequence) { return VectorUtil::containsAll(sequence->tags, tags); });
    }

    std::string GetRandomSequenceWithAllSequenceTagsCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string tags) {
        return GetRandomSequenceWithAllSequenceTags(sft, actors, StringUtil::toTagVector(tags));
    }


    std::string GetRandomFurnitureSequenceWithSequenceTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tag) {
        StringUtil::toLower(&tag);
        return randomSequence(actors, furnitureType, [&tag](Graph::Sequence* sequence) { return VectorUtil::contains(sequence->tags, tag); });
    }

    std::string GetRandomFurnitureSequenceWithAnySequenceTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomSequence(actors, furnitureType, [&tags](Graph::Sequence* sequence) { return VectorUtil::containsAny(sequence->tags, tags); });
    }

    std::string GetRandomFurnitureSequenceWithAnySequenceTagCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tags) {
        return GetRandomFurnitureSequenceWithAnySequenceTag(sft, actors, furnitureType, StringUtil::toTagVector(tags));
    }

    std::string GetRandomFurnitureSequenceWithAllSequenceTags(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomSequence(actors, furnitureType, [&tags](Graph::Sequence* sequence) { return VectorUtil::containsAll(sequence->tags, tags); });
    }

    std::string GetRandomFurnitureSequenceWithAllSequenceTagsCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tags) {
        return GetRandomFurnitureSequenceWithAllSequenceTags(sft, actors, furnitureType, StringUtil::toTagVector(tags));
    }


    bool Bind(VM* a_vm) {
        const auto obj = "OSequence"sv;

        BIND(GetRandomSequence);
        BIND(GetRandomFurnitureSequence);

        BIND(GetRandomSequenceWithSequenceTag);
        BIND(GetRandomSequenceWithAnySequenceTag);
        BIND(GetRandomSequenceWithAnySequenceTagCSV);
        BIND(GetRandomSequenceWithAllSequenceTags);
        BIND(GetRandomSequenceWithAllSequenceTagsCSV);

        BIND(GetRandomFurnitureSequenceWithSequenceTag);
        BIND(GetRandomFurnitureSequenceWithAnySequenceTag);
        BIND(GetRandomFurnitureSequenceWithAnySequenceTagCSV);
        BIND(GetRandomFurnitureSequenceWithAllSequenceTags);
        BIND(GetRandomFurnitureSequenceWithAllSequenceTagsCSV);

        return true;
    }

}