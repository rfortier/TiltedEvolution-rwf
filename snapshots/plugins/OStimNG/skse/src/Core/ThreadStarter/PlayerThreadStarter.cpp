#include "PlayerThreadStarter.h"

#include "Core/ThreadManager.h"

#include "Furniture/FurnitureTable.h"
#include "GameAPI/Game.h"
#include "GameAPI/GameCamera.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"
#include "Util/ActorUtil.h"
#include "Util/CompatibilityTable.h"

namespace Threading {
    std::vector<std::string> positions{"Dom", "Sub", "Third", "Fourth", "Fifth", "Sixth", "Seventh", "Eigth", "Ninth"};

    void startPlayerThread(ThreadStartParams params) {
        std::thread startThread = std::thread([params] {
            bool inDialogue = getInDialogue(params.actors);
            while (inDialogue) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                inDialogue = getInDialogue(params.actors);
            }

            GameAPI::Game::runSynced([params]() {
                handleFurniture(params);
            });
        });
        startThread.detach();
    }

    bool getInDialogue(std::vector<GameAPI::GameActor> actors) {
        for (GameAPI::GameActor actor : actors) {
            if (actor.isInDialogue()) {
                return true;
            }
        }
        return false;
    }

    void handleFurniture(ThreadStartParams params) {
        if (!params.noFurniture && !params.furniture && MCM::MCMTable::useFurniture()) {
            if (!params.startingNodes.empty()) {
                GameAPI::GameObject bed = Furniture::findFurniture(Furniture::FurnitureTable::getFurnitureType("bed"), params.actors[0].form, MCM::MCMTable::furnitureSearchDistance(), 96.0f);
                if (bed) {
                    if (MCM::MCMTable::selectFurniture()) {
                        GameAPI::Game::showMessageBox("$ostim_message_use_bed", {"$ostim_message_yes", "$ostim_message_no"}, [params, bed](unsigned int result) {
                            if (result == 0) {
                                addFurniture(params, bed);
                            } else {
                                handleActorSorting(params);
                            }
                        });
                    } else {
                        addFurniture(params, bed);
                    }
                } else {
                    handleActorAdding(params);
                }
            } else {
                Furniture::selectFurniture(params.actors.size(), params.actors[0].getPosition(), MCM::MCMTable::furnitureSearchDistance(), 96.0f, false, [params](GameAPI::GameObject furniture) {
                    if (furniture) {
                        addFurniture(params, furniture);
                    } else {
                        handleActorAdding(params);                       
                    }
                });
            }
        } else {
            handleActorAdding(params);
        }
    }

    void addFurniture(ThreadStartParams params, GameAPI::GameObject furniture) {
        params.furniture = furniture;
        handleActorAdding(params);
    }

    void handleActorAdding(ThreadStartParams params) {
        if (!params.startingNodes.empty()) {
            startInner(params);
            return;
        }

        if (!MCM::MCMTable::addActorsAtStart()) {
            handleActorSorting(params);
            return;
        }

        handleActorAddingInner(params);
    }

    void handleActorAddingInner(ThreadStartParams params) {
        if (!Graph::GraphTable::hasNodes(Furniture::FurnitureTable::getFurnitureType(params.furniture, false), params.actors.size() + 1)) {
            handleActorSorting(params);
            return;
        }

        std::vector<GameAPI::GameActor> actors = GameAPI::GameActor::getPlayer().getNearbyActors(2000, [&params](GameAPI::GameActor actor){
            return !VectorUtil::contains(params.actors, actor) && Threading::isEligible(actor);    
        });

        if (actors.empty()) {
            handleActorSorting(params);
            return;
        }

        std::vector<std::string> options = {"$ostim_message_none"};
        int max = std::min<int>(GameAPI::Game::getMessageBoxOptionLimit() - 1, actors.size());
        int i = 0;
        while (i < max) {
            options.push_back(actors[i].getName());
            i++;
        }

        GameAPI::Game::showMessageBox("$ostim_message_add_actor", options, [params, actors](unsigned int result) {
            if (result > 0 && result <= actors.size()) {
                addActor(params, actors[result - 1]);
            } else {
                handleActorSorting(params);
            }
        });
    }

    void addActor(ThreadStartParams params, GameAPI::GameActor actor) {
        params.actors.push_back(actor);
        handleActorAddingInner(params);
    }

    void handleActorSorting(ThreadStartParams params) {
        if (!params.startingNodes.empty()) {
            startInner(params);
            return;
        }

        bool selectRole = false;
        if (params.actors.size() >= 3) {
            selectRole = MCM::MCMTable::playerSelectRoleThreesome();
        } else if (params.actors.size() == 2) {
            if (Compatibility::CompatibilityTable::hasSchlong(params.actors[0]) == Compatibility::CompatibilityTable::hasSchlong(params.actors[1])) {
                selectRole = MCM::MCMTable::playerSelectRoleGay();
            } else {
                selectRole = MCM::MCMTable::playerSelectRoleStraight();
            }
        }

        if (selectRole) {
            std::vector<std::string> options;
            for (int i = 0; i < params.actors.size(); i++) {
                options.push_back(positions[i]);
            }
            GameAPI::Game::showMessageBox("$ostim_message_select_position", options, [params](unsigned int result) {
                sortActors(params, result);
            });
        } else {
            sortActors(params, -1);
        }
    }

    void sortActors(ThreadStartParams params, int playerIndex) {
        ActorUtil::sort(params.actors, params.dominantActors, playerIndex);
        handleStartingNode(params);
    }

    void handleStartingNode(ThreadStartParams params) {
        if (params.startingNodes.empty()) {
            std::string nodeTag = MCM::MCMTable::useIntroScenes() ? "intro" : "idle";
            Furniture::FurnitureType* furnitureType = Furniture::FurnitureTable::getFurnitureType(params.furniture, false);
            std::string furnitureTypeID = furnitureType->getListTypeInternal()->id;

            Graph::Node* node = nullptr;
            if (furnitureTypeID == "none") {
                node = Graph::GraphTable::getRandomNode(furnitureType, Trait::ActorCondition::create(params.actors), [&nodeTag](Graph::Node* node) { return node->hasTag(nodeTag) && node->hasActorTagOnAny("standing"); });
            } else if (furnitureTypeID == "bed") {
                node = Graph::GraphTable::getRandomNode(furnitureType, Trait::ActorCondition::create(params.actors), [&nodeTag](Graph::Node* node) { return node->hasTag(nodeTag) && !node->hasActorTagOnAny("standing"); });
            } else {
                node = Graph::GraphTable::getRandomNode(furnitureType, Trait::ActorCondition::create(params.actors), [&nodeTag](Graph::Node* node) { return node->hasTag(nodeTag); });
            }

            if (node) {
                params.startingNodes.push_back({node->animationLengthMs, node});
            } else {
                GameAPI::Game::notification("no starting animation found");
                return;
            }
        }

        startInner(params);
    }

    void startInner(ThreadStartParams params) {
        logger::info("starting scene");
        if (MCM::MCMTable::useFades()) {
            std::thread fadeThread = std::thread([params] {
                GameAPI::GameCamera::fadeToBlack(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                GameAPI::Game::runSynced([params] { ThreadManager::GetSingleton()->startThread(params); });
                std::this_thread::sleep_for(std::chrono::milliseconds(550));
                GameAPI::GameCamera::fadeFromBlack(1);
            });
            fadeThread.detach();
        } else {
            ThreadManager::GetSingleton()->startThread(params);
        }
    }
}