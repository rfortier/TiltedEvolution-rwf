#include "NPCThreadStarter.h"

#include "Core/ThreadManager.h"

#include "Furniture/FurnitureTable.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"
#include "Util/ActorUtil.h"

namespace Threading {
    int startNPCThread(ThreadStartParams& params) {
        if (!params.noFurniture && !params.furniture && MCM::MCMTable::useFurniture()) {
            if (!params.startingNodes.empty()) {
                params.furniture = Furniture::findFurniture(Furniture::FurnitureTable::getFurnitureType("bed"), params.actors[0].form, MCM::MCMTable::furnitureSearchDistance(), 96.0f);
            } else {
                std::vector<std::pair<Furniture::FurnitureType*, GameAPI::GameObject>> furniture = Furniture::findFurniture(params.actors.size(), params.actors[0].getPosition(), MCM::MCMTable::furnitureSearchDistance(), 96.0f);
                if (!furniture.empty()) {
                    params.furniture = furniture.front().second;
                }
            }
        }

        if (params.startingNodes.empty()) {
            ActorUtil::sort(params.actors, params.dominantActors, -1);
        }

        if (params.startingNodes.empty()) {
            Furniture::FurnitureType* furnitureType = Furniture::FurnitureTable::getFurnitureType(params.furniture, false);
            std::string furnitureTypeID = furnitureType->getListTypeInternal()->id;

            Graph::Node* node = nullptr;
            if (furnitureTypeID == "none") {
                node = Graph::GraphTable::getRandomNode(furnitureType, Trait::ActorCondition::create(params.actors), [&](Graph::Node* node) { return node->hasActorTagOnAny("standing"); });
            } else if (furnitureTypeID == "bed") {
                node = Graph::GraphTable::getRandomNode(furnitureType, Trait::ActorCondition::create(params.actors), [&](Graph::Node* node) { return !node->hasActorTagOnAny("standing"); });
            } else {
                node = Graph::GraphTable::getRandomNode(furnitureType, Trait::ActorCondition::create(params.actors), [&](Graph::Node*) { return true; });
            }

            if (node) {
                params.startingNodes.push_back({node->animationLengthMs, node});
            } else {
                logger::info("no starting node found");
                return -1;
            }
        }

        return ThreadManager::GetSingleton()->startThread(params);

        return -1;
    }
}