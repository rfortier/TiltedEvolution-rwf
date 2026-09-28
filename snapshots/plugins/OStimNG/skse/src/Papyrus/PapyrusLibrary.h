#pragma once

#include "Furniture/FurnitureTable.h"
#include "Graph/GraphTable.h"
#include "ScriptAPI/LibraryScript.h"
#include "Trait/Condition.h"
#include "Util/StringUtil.h"
#include "Util/VectorUtil.h"

namespace PapyrusLibrary {
    using VM = RE::BSScript::IVirtualMachine;

    std::string randomScene(std::vector<RE::Actor*> actors, std::string furnitureType, std::function<bool(Graph::Node*)> condition) {
        if (Graph::Node* node = Graph::GraphTable::getRandomNode(Furniture::FurnitureTable::getFurnitureType(furnitureType), Trait::ActorCondition::create(GameAPI::GameActor::convertVector(actors)), condition)) {
            return node->scene_id;
        }
        
        return "";
    }

    std::string randomScene(std::vector<RE::Actor*> actors, std::function<bool(Graph::Node*)> condition) {
        return randomScene(actors, "", condition);
    }

    bool forAnyLoop(int max, std::function<bool(int)> condition) {
        for (int i = 0; i < max; i++) {
            if (condition(i)) {
                return true;
            }
        }
        return false;
    }

    bool forAllLoop(int max, std::function<bool(int)> condition) {
        for (int i = 0; i < max; i++) {
            if (!condition(i)) {
                return false;
            }
        }
        return true;
    }

    // *********************************************************
    // helper functions specifically for superload
    // *********************************************************
    bool hasAction(Graph::Node* node, std::string type, std::vector<int> actors, std::vector<int> targets, std::vector<int> performers, std::vector<int> matesAny, std::vector<int> matesAll, std::vector<int> participantsAny, std::vector<int> participantsAll) {
        for (Graph::Action::Action action : node->actions) {
            if ((action.isType(type) || action.attributes->hasTag(type)) &&
                (actors.empty() || VectorUtil::contains(actors, action.roles.actor)) &&
                (targets.empty() || VectorUtil::contains(targets, action.roles.target)) &&
                (performers.empty() || VectorUtil::contains(performers, action.roles.performer)) &&
                (matesAny.empty() || VectorUtil::containsAny(matesAny, {action.roles.actor, action.roles.target})) &&
                (matesAll.empty() || VectorUtil::containsAll(matesAll, {action.roles.actor, action.roles.target})) &&
                (participantsAny.empty() || VectorUtil::containsAny(participantsAny, {action.roles.actor, action.roles.target, action.roles.performer})) &&
                (participantsAll.empty() || VectorUtil::containsAll(participantsAll, {action.roles.actor, action.roles.target, action.roles.performer}))) {
                return true;
            }
        }

        return false;
    }

    bool hasAnyAction(Graph::Node* node, std::vector<std::string> types, std::vector<std::vector<int>> actors, std::vector<std::vector<int>> targets, std::vector<std::vector<int>> performers, std::vector<std::vector<int>> matesAny, std::vector<std::vector<int>> matesAll, std::vector<std::vector<int>> participantsAny, std::vector<std::vector<int>> participantsAll) {
        for (int i = 0; i < types.size(); i++) {
            if (hasAction(node, types[i], VectorUtil::getElementOrEmpty(actors, i), VectorUtil::getElementOrEmpty(targets, i), VectorUtil::getElementOrEmpty(performers, i), VectorUtil::getElementOrEmpty(matesAny, i), VectorUtil::getElementOrEmpty(matesAll, i), VectorUtil::getElementOrEmpty(participantsAny, i), VectorUtil::getElementOrEmpty(participantsAll, i))) {
                return true;
            }
        }

        return false;
    }

    bool hasAllActions(Graph::Node* node, std::vector<std::string> types, std::vector<std::vector<int>> actors, std::vector<std::vector<int>> targets, std::vector<std::vector<int>> performers, std::vector<std::vector<int>> matesAny, std::vector<std::vector<int>> matesAll, std::vector<std::vector<int>> participantsAny, std::vector<std::vector<int>> participantsAll) {
        for (int i = 0; i < types.size(); i++) {
            if (!hasAction(node, types[i], VectorUtil::getElementOrEmpty(actors, i), VectorUtil::getElementOrEmpty(targets, i), VectorUtil::getElementOrEmpty(performers, i), VectorUtil::getElementOrEmpty(matesAny, i), VectorUtil::getElementOrEmpty(matesAll, i), VectorUtil::getElementOrEmpty(participantsAny, i), VectorUtil::getElementOrEmpty(participantsAll, i))) {
                return false;
            }
        }

        return true;
    }

    bool isActionListed(Graph::Action::Action action, std::vector<std::string> types, std::vector<std::vector<int>> actors, std::vector<std::vector<int>> targets, std::vector<std::vector<int>> performers, std::vector<std::vector<int>> matesAny, std::vector<std::vector<int>> matesAll, std::vector<std::vector<int>> participantsAny, std::vector<std::vector<int>> participantsAll) {
        for (int i = 0; i < types.size(); i++) {
            if (action.isType(types[i]) || action.attributes->hasTag(types[i])) {
                std::vector<int> actorsList = VectorUtil::getElementOrEmpty(actors, i);
                std::vector<int> targetsList = VectorUtil::getElementOrEmpty(targets, i);
                std::vector<int> performersList = VectorUtil::getElementOrEmpty(performers, i);
                std::vector<int> matesAnyList = VectorUtil::getElementOrEmpty(matesAny, i);
                std::vector<int> matesAllList = VectorUtil::getElementOrEmpty(matesAll, i);
                std::vector<int> participantsAnyList = VectorUtil::getElementOrEmpty(participantsAny, i);
                std::vector<int> participantsAllList = VectorUtil::getElementOrEmpty(participantsAll, i);

                if ((actorsList.empty() || VectorUtil::contains(actorsList, action.roles.actor)) &&
                    (targetsList.empty() || VectorUtil::contains(targetsList, action.roles.target)) &&
                    (performersList.empty() || VectorUtil::contains(performersList, action.roles.performer)) &&
                    (matesAnyList.empty() || VectorUtil::containsAny(matesAnyList, {action.roles.actor, action.roles.target})) &&
                    (matesAllList.empty() || VectorUtil::containsAll(matesAllList, {action.roles.actor, action.roles.target})) &&
                    (participantsAnyList.empty() || VectorUtil::containsAny(participantsAnyList, {action.roles.actor, action.roles.target, action.roles.performer})) &&
                    (participantsAllList.empty() || VectorUtil::containsAll(participantsAllList, {action.roles.actor, action.roles.target, action.roles.performer}))) {
                    return true;
                }
                
            }
        }
        return false;
    }

    bool hasOnlyListedActions(Graph::Node* node, std::vector<std::string> types, std::vector<std::vector<int>> actors, std::vector<std::vector<int>> targets, std::vector<std::vector<int>> performers, std::vector<std::vector<int>> matesAny, std::vector<std::vector<int>> matesAll, std::vector<std::vector<int>> participantsAny, std::vector<std::vector<int>> participantsAll) {
        for (Graph::Action::Action action : node->actions) {
            if (!isActionListed(action, types, actors, targets, performers, matesAny, matesAll, participantsAny, participantsAll)) {
                return false;
            }
        }
        return true;
    }

    bool checkConditions(std::vector<std::function<bool(Graph::Node*)>> &conditions, Graph::Node* node) {
        for (std::function<bool(Graph::Node*)> condition : conditions) {
            if (!condition(node)) {
                return false;
            }
        }
        return true;
    }

    // *********************************************************
    // start of papyrus bound functions
    // *********************************************************

    std::vector<std::string> GetAllScenes(RE::StaticFunctionTag*) {
        return ScriptAPI::Library::getAllNodes();
    }

    std::vector<std::string> GetScenesInRange(RE::StaticFunctionTag*, std::string id, std::vector<RE::Actor*> actors, int distance) {
        return ScriptAPI::Library::getNodesInRange(id, GameAPI::GameActor::convertVector(actors), distance);
    }

    std::string GetRandomScene(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors) {
        return randomScene(actors, [](Graph::Node* node) { return true; });
    }

    std::string GetRandomFurnitureScene(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType) {
        return randomScene(actors, furnitureType, [](Graph::Node* node) { return true; });
    }


    std::string GetRandomSceneWithSceneTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string tag) {
        StringUtil::toLower(&tag);
        return randomScene(actors, [tag](Graph::Node* node) { return node->hasTag(tag); });
    }

    std::string GetRandomSceneWithAnySceneTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, [tags](Graph::Node* node) { return node->hasAnyTag(tags); });
    }

    std::string GetRandomSceneWithAnySceneTagCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string tags) {
        return GetRandomSceneWithAnySceneTag(sft, actors, StringUtil::toTagVector(tags));
    }

    std::string GetRandomSceneWithAllSceneTags(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, [tags](Graph::Node* node) { return node->hasAllTags(tags); });
    }

    std::string GetRandomSceneWithAllSceneTagsCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string tags) {
        return GetRandomSceneWithAllSceneTags(sft, actors, StringUtil::toTagVector(tags));
    }


    std::string GetRandomFurnitureSceneWithSceneTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tag) {
        StringUtil::toLower(&tag);
        return randomScene(actors, furnitureType, [tag](Graph::Node* node) { return node->hasTag(tag); });
    }

    std::string GetRandomFurnitureSceneWithAnySceneTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, furnitureType, [tags](Graph::Node* node) { return node->hasAnyTag(tags); });
    }

    std::string GetRandomFurnitureSceneWithAnySceneTagCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tags) {
        return GetRandomFurnitureSceneWithAnySceneTag(sft, actors, furnitureType, StringUtil::toTagVector(tags));
    }

    std::string GetRandomFurnitureSceneWithAllSceneTags(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, furnitureType, [tags](Graph::Node* node) { return node->hasAllTags(tags); });
    }

    std::string GetRandomFurnitureSceneWithAllSceneTagsCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tags) {
        return GetRandomFurnitureSceneWithAllSceneTags(sft, actors, furnitureType, StringUtil::toTagVector(tags));
    }
    

    std::string GetRandomSceneWithSingleActorTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::string tag) {
        StringUtil::toLower(&tag);
        return randomScene(actors, [position, tag](Graph::Node* node) { return node->hasActorTag(position, tag); });
    }

    std::string GetRandomSceneWithAnySingleActorTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, [position, tags](Graph::Node* node) { return node->hasAnyActorTag(position, tags); });
    }

    std::string GetRandomSceneWithAnySingleActorTagCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, int position, std::string tags) {
        return GetRandomSceneWithAnySingleActorTag(sft, actors, position, StringUtil::toTagVector(tags));
    }

    std::string GetRandomSceneWithAllSingleActorTags(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, [position, tags](Graph::Node* node) { return node->hasAllActorTags(position, tags); });
    }

    std::string GetRandomSceneWithAllSingleActorTagsCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, int position, std::string tags) {
        return GetRandomSceneWithAllSingleActorTags(sft, actors, position, StringUtil::toTagVector(tags));
    }
    

    std::string GetRandomFurnitureSceneWithSingleActorTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string tag) {
        StringUtil::toLower(&tag);
        return randomScene(actors, furnitureType, [position, tag](Graph::Node* node) { return node->hasActorTag(position, tag); });
    }

    std::string GetRandomFurnitureSceneWithAnySingleActorTag(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, furnitureType, [position, tags](Graph::Node* node) { return node->hasAnyActorTag(position, tags); });
    }

    std::string GetRandomFurnitureSceneWithAnySingleActorTagCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string tags) {
        return GetRandomFurnitureSceneWithAnySingleActorTag(sft, actors, furnitureType, position, StringUtil::toTagVector(tags));
    }

    std::string GetRandomFurnitureSceneWithAllSingleActorTags(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, furnitureType, [position, tags](Graph::Node* node) { return node->hasAllActorTags(position, tags); });
    }

    std::string GetRandomFurnitureSceneWithAllSingleActorTagsCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string tags) {
        return GetRandomFurnitureSceneWithAllSingleActorTags(sft, actors, furnitureType, position, StringUtil::toTagVector(tags));
    }


    std::string GetRandomSceneWithMultiActorTagForAny(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, [tags](Graph::Node* node) { return forAnyLoop(tags.size(), [node, tags](int i) {return node->hasActorTag(i, tags[i]);}); });
    }

    std::string GetRandomSceneWithMultiActorTagForAnyCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string tags) {
        return GetRandomSceneWithMultiActorTagForAny(sft, actors, StringUtil::toTagVector(tags));
    }

    std::string GetRandomSceneWithMultiActorTagForAll(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, [tags](Graph::Node* node) { return forAllLoop(tags.size(), [node, tags](int i) {return node->hasActorTag(i, tags[i]);}); });
    }

    std::string GetRandomSceneWithMultiActorTagForAllCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string tags) {
        return GetRandomSceneWithMultiActorTagForAll(sft, actors, StringUtil::toTagVector(tags));
    }

    std::string GetRandomSceneWithAnyMultiActorTagForAnyCSV(RE::StaticFunctionTag*, std::vector < RE::Actor*> actors, std::string tags) {
        auto matrix = StringUtil::toTagMatrix(tags);
        return randomScene(actors, [matrix](Graph::Node* node) { return forAnyLoop(matrix.size(), [node, matrix](int i) {return node->hasAnyActorTag(i, matrix[i]);}); });
    }

    std::string GetRandomSceneWithAnyMultiActorTagForAllCSV(RE::StaticFunctionTag*, std::vector < RE::Actor*> actors, std::string tags) {
        auto matrix = StringUtil::toTagMatrix(tags);
        return randomScene(actors, [matrix](Graph::Node* node) { return forAllLoop(matrix.size(), [node, matrix](int i) {return node->hasAnyActorTag(i, matrix[i]);}); });
    }

    std::string GetRandomSceneWithAllMultiActorTagsForAnyCSV(RE::StaticFunctionTag*, std::vector < RE::Actor*> actors, std::string tags) {
        auto matrix = StringUtil::toTagMatrix(tags);
        return randomScene(actors, [matrix](Graph::Node* node) { return forAnyLoop(matrix.size(), [node, matrix](int i) {return node->hasAllActorTags(i, matrix[i]);}); });
    }

    std::string GetRandomSceneWithAllMultiActorTagsForAllCSV(RE::StaticFunctionTag*, std::vector < RE::Actor*> actors, std::string tags) {
        auto matrix = StringUtil::toTagMatrix(tags);
        return randomScene(actors, [matrix](Graph::Node* node) { return forAllLoop(matrix.size(), [node, matrix](int i) {return node->hasAllActorTags(i, matrix[i]);}); });
    }


    std::string GetRandomFurnitureSceneWithMultiActorTagForAny(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, furnitureType, [tags](Graph::Node* node) { return forAnyLoop(tags.size(), [node, tags](int i) {return node->hasActorTag(i, tags[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithMultiActorTagForAnyCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tags) {
        return GetRandomFurnitureSceneWithMultiActorTagForAny(sft, actors, furnitureType, StringUtil::toTagVector(tags));
    }

    std::string GetRandomFurnitureSceneWithMultiActorTagForAll(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::vector<std::string> tags) {
        StringUtil::toLower(&tags);
        return randomScene(actors, furnitureType, [tags](Graph::Node* node) { return forAllLoop(tags.size(), [node, tags](int i) {return node->hasActorTag(i, tags[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithMultiActorTagForAllCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tags) {
        return GetRandomFurnitureSceneWithMultiActorTagForAll(sft, actors, furnitureType, StringUtil::toTagVector(tags));
    }

    std::string GetRandomFurnitureSceneWithAnyMultiActorTagForAnyCSV(RE::StaticFunctionTag*, std::vector <RE::Actor*> actors, std::string furnitureType, std::string tags) {
        auto matrix = StringUtil::toTagMatrix(tags);
        return randomScene(actors, furnitureType, [matrix](Graph::Node* node) { return forAnyLoop(matrix.size(), [node, matrix](int i) {return node->hasAnyActorTag(i, matrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAnyMultiActorTagForAllCSV(RE::StaticFunctionTag*, std::vector <RE::Actor*> actors, std::string furnitureType, std::string tags) {
        auto matrix = StringUtil::toTagMatrix(tags);
        return randomScene(actors, furnitureType, [matrix](Graph::Node* node) { return forAllLoop(matrix.size(), [node, matrix](int i) {return node->hasAnyActorTag(i, matrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAllMultiActorTagsForAnyCSV(RE::StaticFunctionTag*, std::vector <RE::Actor*> actors, std::string furnitureType, std::string tags) {
        auto matrix = StringUtil::toTagMatrix(tags);
        return randomScene(actors, furnitureType, [matrix](Graph::Node* node) { return forAnyLoop(matrix.size(), [node, matrix](int i) {return node->hasAllActorTags(i, matrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAllMultiActorTagsForAllCSV(RE::StaticFunctionTag*, std::vector <RE::Actor*> actors, std::string furnitureType, std::string tags) {
        auto matrix = StringUtil::toTagMatrix(tags);
        return randomScene(actors, furnitureType, [matrix](Graph::Node* node) { return forAllLoop(matrix.size(), [node, matrix](int i) {return node->hasAllActorTags(i, matrix[i]);}); });
    }


    std::string GetRandomSceneWithAnySceneTagAndAnyMultiActorTagForAnyCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAnyTag(sceneVector) && forAnyLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAnyActorTag(i, actorMatrix[i]);}); });
    }

    std::string GetRandomSceneWithAllSceneTagsAndAnyMultiActorTagForAnyCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAllTags(sceneVector) && forAnyLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAnyActorTag(i, actorMatrix[i]);}); });
    }

    std::string GetRandomSceneWithAnySceneTagAndAnyMultiActorTagForAllCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAnyTag(sceneVector) && forAllLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAnyActorTag(i, actorMatrix[i]);}); });
    }

    std::string GetRandomSceneWithAllSceneTagsAndAnyMultiActorTagForAllCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAllTags(sceneVector) && forAllLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAnyActorTag(i, actorMatrix[i]);}); });
    }

    std::string GetRandomSceneWithAnySceneTagAndAllMultiActorTagsForAnyCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAnyTag(sceneVector) && forAnyLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAllActorTags(i, actorMatrix[i]);}); });
    }

    std::string GetRandomSceneWithAllSceneTagsAndAllMultiActorTagsForAnyCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAllTags(sceneVector) && forAnyLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAllActorTags(i, actorMatrix[i]);}); });
    }

    std::string GetRandomSceneWithAnySceneTagAndAllMultiActorTagsForAllCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAnyTag(sceneVector) && forAllLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAllActorTags(i, actorMatrix[i]);}); });
    }

    std::string GetRandomSceneWithAllSceneTagsAndAllMultiActorTagsForAllCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAllTags(sceneVector) && forAllLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAllActorTags(i, actorMatrix[i]);}); });
    }


    std::string GetRandomFurnitureSceneWithAnySceneTagAndAnyMultiActorTagForAnyCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, furnitureType, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAnyTag(sceneVector) && forAnyLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAnyActorTag(i, actorMatrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAllSceneTagsAndAnyMultiActorTagForAnyCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, furnitureType, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAllTags(sceneVector) && forAnyLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAnyActorTag(i, actorMatrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAnySceneTagAndAnyMultiActorTagForAllCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, furnitureType, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAnyTag(sceneVector) && forAllLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAnyActorTag(i, actorMatrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAllSceneTagsAndAnyMultiActorTagForAllCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, furnitureType, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAllTags(sceneVector) && forAllLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAnyActorTag(i, actorMatrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAnySceneTagAndAllMultiActorTagsForAnyCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, furnitureType, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAnyTag(sceneVector) && forAnyLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAllActorTags(i, actorMatrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAllSceneTagsAndAllMultiActorTagsForAnyCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, furnitureType, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAllTags(sceneVector) && forAnyLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAllActorTags(i, actorMatrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAnySceneTagAndAllMultiActorTagsForAllCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, furnitureType, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAnyTag(sceneVector) && forAllLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAllActorTags(i, actorMatrix[i]);}); });
    }

    std::string GetRandomFurnitureSceneWithAllSceneTagsAndAllMultiActorTagsForAllCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string sceneTags, std::string actorTags) {
        auto sceneVector = StringUtil::toTagVector(sceneTags);
        auto actorMatrix = StringUtil::toTagMatrix(actorTags);
        return randomScene(actors, furnitureType, [sceneVector, actorMatrix](Graph::Node* node) { return node->hasAllTags(sceneVector) && forAllLoop(actorMatrix.size(), [node, actorMatrix](int i){return node->hasAllActorTags(i, actorMatrix[i]);}); });
    }


    std::string GetRandomSceneWithAction(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string type) {
        StringUtil::toLower(&type);
        return randomScene(actors, [type](Graph::Node* node) { return node->findAction(type) != -1; });
    }

    std::string GetRandomSceneWithAnyAction(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, [types](Graph::Node* node) { return node->findAnyAction(types) != -1; });
    }

    std::string GetRandomSceneWithAnyActionCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string types) {
        return GetRandomSceneWithAnyAction(sft, actors, StringUtil::toTagVector(types));
    }

    std::string GetRandomSceneWithAllActions(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, [types](Graph::Node* node) { return forAllLoop(types.size(), [node, types](int i){return node->findAction(types[i]) != -1;}); });
    }

    std::string GetRandomSceneWithAllActionsCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string tags) {
        return GetRandomSceneWithAllActions(sft, actors, StringUtil::toTagVector(tags));
    }


    std::string GetRandomFurnitureSceneWithAction(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string type) {
        StringUtil::toLower(&type);
        return randomScene(actors, furnitureType, [type](Graph::Node* node) { return node->findAction(type) != -1; });
    }

    std::string GetRandomFurnitureSceneWithAnyAction(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, furnitureType, [types](Graph::Node* node) { return node->findAnyAction(types) != -1; });
    }

    std::string GetRandomFurnitureSceneWithAnyActionCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, std::string types) {
        return GetRandomFurnitureSceneWithAnyAction(sft, actors, furnitureType, StringUtil::toTagVector(types));
    }

    std::string GetRandomFurnitureSceneWithAllActions(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, furnitureType, [types](Graph::Node* node) { return forAllLoop(types.size(), [node, types](int i){return node->findAction(types[i]) != -1;}); });
    }

    std::string GetRandomFurnitureSceneWithAllActionsCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, std::string tags) {
        return GetRandomFurnitureSceneWithAllActions(sft, actors, furnitureType, StringUtil::toTagVector(tags));
    }


    std::string GetRandomSceneWithActionForActor(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::string type) {
        StringUtil::toLower(&type);
        return randomScene(actors, [position, type](Graph::Node* node) { return node->findActionForActor(position, type) != -1; });
    }

    std::string GetRandomSceneWithAnyActionForActor(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, [position, types](Graph::Node* node) { return node->findAnyActionForActor(position, types) != -1; });
    }

    std::string GetRandomSceneWithAnyActionForActorCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, int position, std::string types) {
        return GetRandomSceneWithAnyActionForActor(sft, actors, position, StringUtil::toTagVector(types));
    }

    std::string GetRandomSceneWithAllActionsForActor(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, [position, types](Graph::Node* node) { return forAllLoop(types.size(), [node, position, types](int i){return node->findActionForActor(position, types[i]) != -1;}); });
    }

    std::string GetRandomSceneWithAllActionsForActorCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, int position, std::string tags) {
        return GetRandomSceneWithAllActionsForActor(sft, actors, position, StringUtil::toTagVector(tags));
    }


    std::string GetRandomFurnitureSceneWithActionForActor(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string type) {
        StringUtil::toLower(&type);
        return randomScene(actors, furnitureType, [position, type](Graph::Node* node) { return node->findActionForActor(position, type) != -1; });
    }

    std::string GetRandomFurnitureSceneWithAnyActionForActor(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, furnitureType, [position, types](Graph::Node* node) { return node->findAnyActionForActor(position, types) != -1; });
    }

    std::string GetRandomFurnitureSceneWithAnyActionForActorCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string types) {
        return GetRandomFurnitureSceneWithAnyActionForActor(sft, actors, furnitureType, position, StringUtil::toTagVector(types));
    }

    std::string GetRandomFurnitureSceneWithAllActionsForActor(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, furnitureType, [position, types](Graph::Node* node) { return forAllLoop(types.size(), [node, position, types](int i){return node->findActionForActor(position, types[i]) != -1;}); });
    }

    std::string GetRandomFurnitureSceneWithAllActionsForActorCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string tags) {
        return GetRandomFurnitureSceneWithAllActionsForActor(sft, actors, furnitureType, position, StringUtil::toTagVector(tags));
    }


    std::string GetRandomSceneWithActionForTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::string type) {
        StringUtil::toLower(&type);
        return randomScene(actors, [position, type](Graph::Node* node) { return node->findActionForTarget(position, type) != -1; });
    }

    std::string GetRandomSceneWithAnyActionForTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, [position, types](Graph::Node* node) { return node->findAnyActionForTarget(position, types) != -1; });
    }

    std::string GetRandomSceneWithAnyActionForTargetCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, int position, std::string types) {
        return GetRandomSceneWithAnyActionForTarget(sft, actors, position, StringUtil::toTagVector(types));
    }

    std::string GetRandomSceneWithAllActionsForTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int position, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, [position, types](Graph::Node* node) { return forAllLoop(types.size(), [node, position, types](int i){return node->findActionForTarget(position, types[i]) != -1;}); });
    }

    std::string GetRandomSceneWithAllActionsForTargetCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, int position, std::string tags) {
        return GetRandomSceneWithAllActionsForTarget(sft, actors, position, StringUtil::toTagVector(tags));
    }


    std::string GetRandomFurnitureSceneWithActionForTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string type) {
        StringUtil::toLower(&type);
        return randomScene(actors, furnitureType, [position, type](Graph::Node* node) { return node->findActionForTarget(position, type) != -1; });
    }

    std::string GetRandomFurnitureSceneWithAnyActionForTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, furnitureType, [position, types](Graph::Node* node) { return node->findAnyActionForTarget(position, types) != -1; });
    }

    std::string GetRandomFurnitureSceneWithAnyActionForTargetCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string types) {
        return GetRandomFurnitureSceneWithAnyActionForTarget(sft, actors, furnitureType, position, StringUtil::toTagVector(types));
    }

    std::string GetRandomFurnitureSceneWithAllActionsForTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, furnitureType, [position, types](Graph::Node* node) { return forAllLoop(types.size(), [node, position, types](int i){return node->findActionForTarget(position, types[i]) != -1;}); });
    }

    std::string GetRandomFurnitureSceneWithAllActionsForTargetCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, int position, std::string tags) {
        return GetRandomFurnitureSceneWithAllActionsForTarget(sft, actors, furnitureType, position, StringUtil::toTagVector(tags));
    }


    std::string GetRandomSceneWithActionForActorAndTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int actorPosition, int targetPosition, std::string type) {
        StringUtil::toLower(&type);
        return randomScene(actors, [actorPosition, targetPosition, type](Graph::Node* node) { return node->findActionForActorAndTarget(actorPosition, targetPosition, type) != -1; });
    }

    std::string GetRandomSceneWithAnyActionForActorAndTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int actorPosition, int targetPosition, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, [actorPosition, targetPosition, types](Graph::Node* node) { return node->findAnyActionForActorAndTarget(actorPosition, targetPosition, types) != -1; });
    }

    std::string GetRandomSceneWithAnyActionForActorAndTargetCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, int actorPosition, int targetPosition, std::string types) {
        return GetRandomSceneWithAnyActionForActorAndTarget(sft, actors, actorPosition, targetPosition, StringUtil::toTagVector(types));
    }

    std::string GetRandomSceneWithAllActionsForActorAndTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, int actorPosition, int targetPosition, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, [actorPosition, targetPosition, types](Graph::Node* node) { return forAllLoop(types.size(), [node, actorPosition, targetPosition, types](int i){return node->findActionForActorAndTarget(actorPosition, targetPosition, types[i]) != -1;}); });
    }

    std::string GetRandomSceneWithAllActionsForActorAndTargetCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, int actorPosition, int targetPosition, std::string tags) {
        return GetRandomSceneWithAllActionsForActorAndTarget(sft, actors, actorPosition, targetPosition, StringUtil::toTagVector(tags));
    }


    std::string GetRandomFurnitureSceneWithActionForActorAndTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int actorPosition, int targetPosition, std::string type) {
        StringUtil::toLower(&type);
        return randomScene(actors, furnitureType, [actorPosition, targetPosition, type](Graph::Node* node) { return node->findActionForActorAndTarget(actorPosition, targetPosition, type) != -1; });
    }

    std::string GetRandomFurnitureSceneWithAnyActionForActorAndTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int actorPosition, int targetPosition, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, furnitureType, [actorPosition, targetPosition, types](Graph::Node* node) { return node->findAnyActionForActorAndTarget(actorPosition, targetPosition, types) != -1; });
    }

    std::string GetRandomFurnitureSceneWithAnyActionForActorAndTargetCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, int actorPosition, int targetPosition, std::string types) {
        return GetRandomFurnitureSceneWithAnyActionForActorAndTarget(sft, actors, furnitureType, actorPosition, targetPosition, StringUtil::toTagVector(types));
    }

    std::string GetRandomFurnitureSceneWithAllActionsForActorAndTarget(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, int actorPosition, int targetPosition, std::vector<std::string> types) {
        StringUtil::toLower(&types);
        return randomScene(actors, furnitureType, [actorPosition, targetPosition, types](Graph::Node* node) { return forAllLoop(types.size(), [node, actorPosition, targetPosition, types](int i){return node->findActionForActorAndTarget(actorPosition, targetPosition, types[i]) != -1;}); });
    }

    std::string GetRandomFurnitureSceneWithAllActionsForActorAndTargetCSV(RE::StaticFunctionTag* sft, std::vector<RE::Actor*> actors, std::string furnitureType, int actorPosition, int targetPosition, std::string tags) {
        return GetRandomFurnitureSceneWithAllActionsForActorAndTarget(sft, actors, furnitureType, actorPosition, targetPosition, StringUtil::toTagVector(tags));
    }


    std::string GetRandomSceneSuperloadCSV(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string furnitureType, std::string anySceneTag, std::string allSceneTags, std::string sceneTagWhitelist, std::string sceneTagBlacklist, std::string anyActorTagForAny, std::string anyActorTagForAll, std::string allActorTagsForAny, std::string allActorTagsForAll, std::string actorTagWhitelistForAny, std::string actorTagWhitelistForAll, std::string actorTagBlacklistForAny, std::string actorTagBlacklistForAll, std::string anyActionType, std::string anyActionActor, std::string anyActionTarget, std::string anyActionPerformer, std::string anyActionMateAny, std::string anyActionMateAll, std::string anyActionParticipantAny, std::string anyActionParticipantAll, std::string allActionTypes, std::string allActionActors, std::string allActionTargets, std::string allActionPerformers, std::string allActionMatesAny, std::string allActionMatesAll, std::string allActionParticipantsAny, std::string allActionParticipantsAll, std::string actionWhitelistTypes, std::string actionWhitelistActors, std::string actionWhitelistTargets, std::string actionWhitelistPerformers, std::string actionWhitelistMatesAny, std::string actionWhitelistMatesAll, std::string actionWhitelistParticipantsAny, std::string actionWhitelistParticipantsAll, std::string actionBlacklistTypes, std::string actionBlacklistActors, std::string actionBlacklistTargets, std::string actionBlacklistPerformers, std::string actionBlacklistMatesAny, std::string actionBlacklistMatesAll, std::string actionBlacklistParticipantsAny, std::string actionBlacklistParticipantsAll) {
        std::vector<std::function<bool(Graph::Node*)>> conditions;
        

        if (!anySceneTag.empty()) {
            std::vector<std::string> anySceneTagVector = StringUtil::toTagVector(anySceneTag);
            conditions.push_back([anySceneTagVector](Graph::Node* node){return node->hasAnyTag(anySceneTagVector);});
        }

        if (!allSceneTags.empty()) {
            std::vector<std::string> allSceneTagsVector = StringUtil::toTagVector(allSceneTags);
            conditions.push_back([allSceneTagsVector](Graph::Node* node){return node->hasAllTags(allSceneTagsVector);});
        }

        if (!sceneTagWhitelist.empty()) {
            std::vector<std::string> sceneTagWhitelistVector = StringUtil::toTagVector(sceneTagWhitelist);
            conditions.push_back([sceneTagWhitelistVector](Graph::Node* node){return node->hasOnlyTags(sceneTagWhitelistVector);});
        }

        if (!sceneTagBlacklist.empty()) {
            std::vector<std::string> sceneTagBlacklistVector = StringUtil::toTagVector(sceneTagBlacklist);
            conditions.push_back([sceneTagBlacklistVector](Graph::Node* node){return !node->hasAnyTag(sceneTagBlacklistVector);});
        }


        if (!anyActorTagForAny.empty()) {
            std::vector<std::vector<std::string>> anyActorTagForAnyMatrix = StringUtil::toTagMatrix(anyActorTagForAny);
            conditions.push_back([anyActorTagForAnyMatrix](Graph::Node* node){return forAnyLoop(anyActorTagForAnyMatrix.size(), [node, anyActorTagForAnyMatrix](int i){return node->hasAnyActorTag(i, anyActorTagForAnyMatrix[i]);});});
        }

        if (!anyActorTagForAll.empty()) {
            std::vector<std::vector<std::string>> anyActorTagForAllMatrix = StringUtil::toTagMatrix(anyActorTagForAll);
            conditions.push_back([anyActorTagForAllMatrix](Graph::Node* node){return forAllLoop(anyActorTagForAllMatrix.size(), [node, anyActorTagForAllMatrix](int i){return node->hasAnyActorTag(i, anyActorTagForAllMatrix[i]);});});
        }

        if (!allActorTagsForAny.empty()) {
            std::vector<std::vector<std::string>> allActorTagsForAnyMatrix = StringUtil::toTagMatrix(allActorTagsForAny);
            conditions.push_back([allActorTagsForAnyMatrix](Graph::Node* node){return forAnyLoop(allActorTagsForAnyMatrix.size(), [node, allActorTagsForAnyMatrix](int i){return node->hasAllActorTags(i, allActorTagsForAnyMatrix[i]);});});
        }

        if (!allActorTagsForAll.empty()) {
            std::vector<std::vector<std::string>> allActorTagsForAllMatrix = StringUtil::toTagMatrix(allActorTagsForAll);
            conditions.push_back([allActorTagsForAllMatrix](Graph::Node* node){return forAllLoop(allActorTagsForAllMatrix.size(), [node, allActorTagsForAllMatrix](int i){return node->hasAllActorTags(i, allActorTagsForAllMatrix[i]);});});
        }

        if (!actorTagWhitelistForAny.empty()) {
            std::vector<std::vector<std::string>> actorTagWhitelistForAnyMatrix = StringUtil::toTagMatrix(actorTagWhitelistForAny);
            conditions.push_back([actorTagWhitelistForAnyMatrix](Graph::Node* node){return forAnyLoop(actorTagWhitelistForAnyMatrix.size(), [node, actorTagWhitelistForAnyMatrix](int i){return node->hasOnlyListedActorTags(i, actorTagWhitelistForAnyMatrix[i]);});});
        }

        if (!actorTagWhitelistForAll.empty()) {
            std::vector<std::vector<std::string>> actorTagWhitelistForAllMatrix = StringUtil::toTagMatrix(actorTagWhitelistForAll);
            conditions.push_back([actorTagWhitelistForAllMatrix](Graph::Node* node){return forAllLoop(actorTagWhitelistForAllMatrix.size(), [node, actorTagWhitelistForAllMatrix](int i){return node->hasOnlyListedActorTags(i, actorTagWhitelistForAllMatrix[i]);});});
        }

        if (!actorTagBlacklistForAny.empty()) {
            std::vector<std::vector<std::string>> actorTagBlacklistForAnyMatrix = StringUtil::toTagMatrix(actorTagBlacklistForAny);
            conditions.push_back([actorTagBlacklistForAnyMatrix](Graph::Node* node){return forAnyLoop(actorTagBlacklistForAnyMatrix.size(), [node, actorTagBlacklistForAnyMatrix](int i){return !node->hasAnyActorTag(i, actorTagBlacklistForAnyMatrix[i]);});});
        }

        if (!actorTagBlacklistForAll.empty()) {
            std::vector<std::vector<std::string>> actorTagBlacklistForAllMatrix = StringUtil::toTagMatrix(actorTagBlacklistForAll);
            conditions.push_back([actorTagBlacklistForAllMatrix](Graph::Node* node){return forAllLoop(actorTagBlacklistForAllMatrix.size(), [node, actorTagBlacklistForAllMatrix](int i){return !node->hasAnyActorTag(i, actorTagBlacklistForAllMatrix[i]);});});
        }


        if (!anyActionType.empty()) {
            std::vector<std::string> anyActionTypeVector = StringUtil::toTagVector(anyActionType);
            std::vector<std::vector<int>> anyActionActorMatrix = VectorUtil::stoim(anyActionActor);
            std::vector<std::vector<int>> anyActionTargetMatrix = VectorUtil::stoim(anyActionTarget);
            std::vector<std::vector<int>> anyActionPerformerMatrix = VectorUtil::stoim(anyActionPerformer);
            std::vector<std::vector<int>> anyActionMateAnyMatrix = VectorUtil::stoim(anyActionMateAny);
            std::vector<std::vector<int>> anyActionMateAllMatrix = VectorUtil::stoim(anyActionMateAll);
            std::vector<std::vector<int>> anyActionParticipantAnyMatrix = VectorUtil::stoim(anyActionParticipantAny);
            std::vector<std::vector<int>> anyActionParticipantAllMatrix = VectorUtil::stoim(anyActionParticipantAll);
            conditions.push_back([anyActionTypeVector, anyActionActorMatrix, anyActionTargetMatrix, anyActionPerformerMatrix, anyActionMateAnyMatrix, anyActionMateAllMatrix, anyActionParticipantAnyMatrix, anyActionParticipantAllMatrix](Graph::Node* node){
                    return hasAnyAction(node, anyActionTypeVector, anyActionActorMatrix, anyActionTargetMatrix, anyActionPerformerMatrix, anyActionMateAnyMatrix, anyActionMateAllMatrix, anyActionParticipantAnyMatrix, anyActionParticipantAllMatrix);
                });
        }
        
        if (!allActionTypes.empty()) {
            std::vector<std::string> allActionTypesVector = StringUtil::toTagVector(allActionTypes);
            std::vector<std::vector<int>> allActionActorsMatrix = VectorUtil::stoim(allActionActors);
            std::vector<std::vector<int>> allActionTargetsMatrix = VectorUtil::stoim(allActionTargets);
            std::vector<std::vector<int>> allActionPerformersMatrix = VectorUtil::stoim(allActionPerformers);
            std::vector<std::vector<int>> allActionMatesAnyMatrix = VectorUtil::stoim(allActionMatesAny);
            std::vector<std::vector<int>> allActionMatesAllMatrix = VectorUtil::stoim(allActionMatesAll);
            std::vector<std::vector<int>> allActionParticipantsAnyMatrix = VectorUtil::stoim(allActionParticipantsAny);
            std::vector<std::vector<int>> allActionParticipantsAllMatrix = VectorUtil::stoim(allActionParticipantsAll);
            conditions.push_back([allActionTypesVector, allActionActorsMatrix, allActionTargetsMatrix, allActionPerformersMatrix, allActionMatesAnyMatrix, allActionMatesAllMatrix, allActionParticipantsAnyMatrix, allActionParticipantsAllMatrix](Graph::Node* node){
                    return hasAllActions(node, allActionTypesVector, allActionActorsMatrix, allActionTargetsMatrix, allActionPerformersMatrix, allActionMatesAnyMatrix, allActionMatesAllMatrix, allActionParticipantsAnyMatrix, allActionParticipantsAllMatrix);
                });
        }

        if (!actionWhitelistTypes.empty()) {
            std::vector<std::string> actionWhitelistTypesVector = StringUtil::toTagVector(actionWhitelistTypes);
            std::vector<std::vector<int>> actionWhitelistActorsMatrix = VectorUtil::stoim(actionWhitelistActors);
            std::vector<std::vector<int>> actionWhitelistTargetsMatrix = VectorUtil::stoim(actionWhitelistTargets);
            std::vector<std::vector<int>> actionWhitelistPerformersMatrix = VectorUtil::stoim(actionWhitelistPerformers);
            std::vector<std::vector<int>> actionWhitelistMatesAnyMatrix = VectorUtil::stoim(actionWhitelistMatesAny);
            std::vector<std::vector<int>> actionWhitelistMatesAllMatrix = VectorUtil::stoim(actionWhitelistMatesAll);
            std::vector<std::vector<int>> actionWhitelistParticipantsAnyMatrix = VectorUtil::stoim(actionWhitelistParticipantsAny);
            std::vector<std::vector<int>> actionWhitelistParticipantsAllMatrix = VectorUtil::stoim(actionWhitelistParticipantsAll);
            conditions.push_back([actionWhitelistTypesVector, actionWhitelistActorsMatrix, actionWhitelistTargetsMatrix, actionWhitelistPerformersMatrix, actionWhitelistMatesAnyMatrix, actionWhitelistMatesAllMatrix, actionWhitelistParticipantsAnyMatrix, actionWhitelistParticipantsAllMatrix](Graph::Node* node){
                    return hasOnlyListedActions(node, actionWhitelistTypesVector, actionWhitelistActorsMatrix, actionWhitelistTargetsMatrix, actionWhitelistPerformersMatrix, actionWhitelistMatesAnyMatrix, actionWhitelistMatesAllMatrix, actionWhitelistParticipantsAnyMatrix, actionWhitelistParticipantsAllMatrix);
                });
        }

        if (!actionBlacklistTypes.empty()) {
            std::vector<std::string> actionBlacklistTypesVector = StringUtil::toTagVector(actionBlacklistTypes);
            std::vector<std::vector<int>> actionBlacklistActorsMatrix = VectorUtil::stoim(actionBlacklistActors);
            std::vector<std::vector<int>> actionBlacklistTargetsMatrix = VectorUtil::stoim(actionBlacklistTargets);
            std::vector<std::vector<int>> actionBlacklistPerformersMatrix = VectorUtil::stoim(actionBlacklistPerformers);
            std::vector<std::vector<int>> actionBlacklistMatesAnyMatrix = VectorUtil::stoim(actionBlacklistMatesAny);
            std::vector<std::vector<int>> actionBlacklistMatesAllMatrix = VectorUtil::stoim(actionBlacklistMatesAll);
            std::vector<std::vector<int>> actionBlacklistParticipantsAnyMatrix = VectorUtil::stoim(actionBlacklistParticipantsAny);
            std::vector<std::vector<int>> actionBlacklistParticipantsAllMatrix = VectorUtil::stoim(actionBlacklistParticipantsAll);
            conditions.push_back([actionBlacklistTypesVector, actionBlacklistActorsMatrix, actionBlacklistTargetsMatrix, actionBlacklistPerformersMatrix, actionBlacklistMatesAnyMatrix, actionBlacklistMatesAllMatrix, actionBlacklistParticipantsAnyMatrix, actionBlacklistParticipantsAllMatrix](Graph::Node* node){
                    return !hasAnyAction(node, actionBlacklistTypesVector, actionBlacklistActorsMatrix, actionBlacklistTargetsMatrix, actionBlacklistPerformersMatrix, actionBlacklistMatesAnyMatrix, actionBlacklistMatesAllMatrix, actionBlacklistParticipantsAnyMatrix, actionBlacklistParticipantsAllMatrix);
                });
        }


        return randomScene(actors, furnitureType, [&conditions](Graph::Node* node) {return checkConditions(conditions, node);});
    }


    bool Bind(VM* a_vm) {
        const auto obj = "OLibrary"sv;

        BIND(GetAllScenes);
        BIND(GetScenesInRange);

        BIND(GetRandomScene);
        BIND(GetRandomFurnitureScene);

        BIND(GetRandomSceneWithSceneTag);
        BIND(GetRandomSceneWithAnySceneTag);
        BIND(GetRandomSceneWithAnySceneTagCSV);
        BIND(GetRandomSceneWithAllSceneTags);
        BIND(GetRandomSceneWithAllSceneTagsCSV);

        BIND(GetRandomFurnitureSceneWithSceneTag);
        BIND(GetRandomFurnitureSceneWithAnySceneTag);
        BIND(GetRandomFurnitureSceneWithAnySceneTagCSV);
        BIND(GetRandomFurnitureSceneWithAllSceneTags);
        BIND(GetRandomFurnitureSceneWithAllSceneTagsCSV);

        BIND(GetRandomSceneWithSingleActorTag);
        BIND(GetRandomSceneWithAnySingleActorTag);
        BIND(GetRandomSceneWithAnySingleActorTagCSV);
        BIND(GetRandomSceneWithAllSingleActorTags);
        BIND(GetRandomSceneWithAllSingleActorTagsCSV);

        BIND(GetRandomFurnitureSceneWithSingleActorTag);
        BIND(GetRandomFurnitureSceneWithAnySingleActorTag);
        BIND(GetRandomFurnitureSceneWithAnySingleActorTagCSV);
        BIND(GetRandomFurnitureSceneWithAllSingleActorTags);
        BIND(GetRandomFurnitureSceneWithAllSingleActorTagsCSV);

        BIND(GetRandomSceneWithMultiActorTagForAny);
        BIND(GetRandomSceneWithMultiActorTagForAnyCSV);
        BIND(GetRandomSceneWithMultiActorTagForAll);
        BIND(GetRandomSceneWithMultiActorTagForAllCSV);
        BIND(GetRandomSceneWithAnyMultiActorTagForAnyCSV);
        BIND(GetRandomSceneWithAnyMultiActorTagForAllCSV);
        BIND(GetRandomSceneWithAllMultiActorTagsForAnyCSV);
        BIND(GetRandomSceneWithAllMultiActorTagsForAllCSV);

        BIND(GetRandomFurnitureSceneWithMultiActorTagForAny);
        BIND(GetRandomFurnitureSceneWithMultiActorTagForAnyCSV);
        BIND(GetRandomFurnitureSceneWithMultiActorTagForAll);
        BIND(GetRandomFurnitureSceneWithMultiActorTagForAllCSV);
        BIND(GetRandomFurnitureSceneWithAnyMultiActorTagForAnyCSV);
        BIND(GetRandomFurnitureSceneWithAnyMultiActorTagForAllCSV);
        BIND(GetRandomFurnitureSceneWithAllMultiActorTagsForAnyCSV);
        BIND(GetRandomFurnitureSceneWithAllMultiActorTagsForAllCSV);

        BIND(GetRandomSceneWithAnySceneTagAndAnyMultiActorTagForAnyCSV);
        BIND(GetRandomSceneWithAllSceneTagsAndAnyMultiActorTagForAnyCSV);
        BIND(GetRandomSceneWithAnySceneTagAndAnyMultiActorTagForAllCSV);
        BIND(GetRandomSceneWithAllSceneTagsAndAnyMultiActorTagForAllCSV);
        BIND(GetRandomSceneWithAnySceneTagAndAllMultiActorTagsForAnyCSV);
        BIND(GetRandomSceneWithAllSceneTagsAndAllMultiActorTagsForAnyCSV);
        BIND(GetRandomSceneWithAnySceneTagAndAllMultiActorTagsForAllCSV);
        BIND(GetRandomSceneWithAllSceneTagsAndAllMultiActorTagsForAllCSV);

        BIND(GetRandomFurnitureSceneWithAnySceneTagAndAnyMultiActorTagForAnyCSV);
        BIND(GetRandomFurnitureSceneWithAllSceneTagsAndAnyMultiActorTagForAnyCSV);
        BIND(GetRandomFurnitureSceneWithAnySceneTagAndAnyMultiActorTagForAllCSV);
        BIND(GetRandomFurnitureSceneWithAllSceneTagsAndAnyMultiActorTagForAllCSV);
        BIND(GetRandomFurnitureSceneWithAnySceneTagAndAllMultiActorTagsForAnyCSV);
        BIND(GetRandomFurnitureSceneWithAllSceneTagsAndAllMultiActorTagsForAnyCSV);
        BIND(GetRandomFurnitureSceneWithAnySceneTagAndAllMultiActorTagsForAllCSV);
        BIND(GetRandomFurnitureSceneWithAllSceneTagsAndAllMultiActorTagsForAllCSV);

        BIND(GetRandomSceneWithAction);
        BIND(GetRandomSceneWithAnyAction);
        BIND(GetRandomSceneWithAnyActionCSV);
        BIND(GetRandomSceneWithAllActions);
        BIND(GetRandomSceneWithAllActionsCSV);

        BIND(GetRandomFurnitureSceneWithAction);
        BIND(GetRandomFurnitureSceneWithAnyAction);
        BIND(GetRandomFurnitureSceneWithAnyActionCSV);
        BIND(GetRandomFurnitureSceneWithAllActions);
        BIND(GetRandomFurnitureSceneWithAllActionsCSV);

        BIND(GetRandomSceneWithActionForActor);
        BIND(GetRandomSceneWithAnyActionForActor);
        BIND(GetRandomSceneWithAnyActionForActorCSV);
        BIND(GetRandomSceneWithAllActionsForActor);
        BIND(GetRandomSceneWithAllActionsForActorCSV);

        BIND(GetRandomFurnitureSceneWithActionForActor);
        BIND(GetRandomFurnitureSceneWithAnyActionForActor);
        BIND(GetRandomFurnitureSceneWithAnyActionForActorCSV);
        BIND(GetRandomFurnitureSceneWithAllActionsForActor);
        BIND(GetRandomFurnitureSceneWithAllActionsForActorCSV);

        BIND(GetRandomSceneWithActionForTarget);
        BIND(GetRandomSceneWithAnyActionForTarget);
        BIND(GetRandomSceneWithAnyActionForTargetCSV);
        BIND(GetRandomSceneWithAllActionsForTarget);
        BIND(GetRandomSceneWithAllActionsForTargetCSV);

        BIND(GetRandomFurnitureSceneWithActionForTarget);
        BIND(GetRandomFurnitureSceneWithAnyActionForTarget);
        BIND(GetRandomFurnitureSceneWithAnyActionForTargetCSV);
        BIND(GetRandomFurnitureSceneWithAllActionsForTarget);
        BIND(GetRandomFurnitureSceneWithAllActionsForTargetCSV);

        BIND(GetRandomSceneWithActionForActorAndTarget);
        BIND(GetRandomSceneWithAnyActionForActorAndTarget);
        BIND(GetRandomSceneWithAnyActionForActorAndTargetCSV);
        BIND(GetRandomSceneWithAllActionsForActorAndTarget);
        BIND(GetRandomSceneWithAllActionsForActorAndTargetCSV);

        BIND(GetRandomFurnitureSceneWithActionForActorAndTarget);
        BIND(GetRandomFurnitureSceneWithAnyActionForActorAndTarget);
        BIND(GetRandomFurnitureSceneWithAnyActionForActorAndTargetCSV);
        BIND(GetRandomFurnitureSceneWithAllActionsForActorAndTarget);
        BIND(GetRandomFurnitureSceneWithAllActionsForActorAndTargetCSV);

        BIND(GetRandomSceneSuperloadCSV);

        return true;
    }
}