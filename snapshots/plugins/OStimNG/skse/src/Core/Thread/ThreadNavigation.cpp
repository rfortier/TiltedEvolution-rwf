#include "Core/Thread.h"

#include "Core/ThreadManager.h"

#include "GameAPI/GameCamera.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"
#include "UI/UIState.h"
#include "Util/Constants.h"
#include "Util/VectorUtil.h"

namespace Threading {

#pragma region util
    // TODO: move this to a ThreadUtil file?
    bool forAnyActor2(Graph::Node* node, std::function<bool(Graph::GraphActor&)> condition) {
        for (Graph::GraphActor& actor : node->actors) {
            if (condition(actor)) {
                return true;
            }
        }
        return false;
    }

    bool forAllActors2(Graph::Node* node, std::function<bool(Graph::GraphActor&)> condition) {
        for (Graph::GraphActor& actor : node->actors) {
            if (!condition(actor)) {
                return false;
            }
        }
        return true;
    }

    void addFurniture2(std::vector<std::function<bool(Graph::Node*)>>& conditions, Furniture::FurnitureType* furnitureType) {
        std::string furnitureTypeID = furnitureType->getListTypeInternal()->id;
        if (furnitureTypeID == "none") {
            conditions.push_back([&](Graph::Node* node) {
                return forAnyActor2(node, [&](Graph::GraphActor& actor) {
                    return actor.hasTag("standing");
                });
            });
        } else if (furnitureTypeID == "bed") {
            conditions.push_back([&](Graph::Node* node) {
                return forAllActors2(node, [&](Graph::GraphActor& actor) {
                    return !actor.hasTag("standing");
                });
            });
        }
    }

    bool checkConditions2(std::vector<std::function<bool(Graph::Node*)>>& conditions, Graph::Node* node) {
        for (std::function<bool(Graph::Node*)> condition : conditions) {
            if (!condition(node)) {
                return false;
            }
        }
        return true;
    }
#pragma endregion

    bool Thread::autoTransition(std::string type) {
        if (!nodeQueue.empty()) {
            return false;
        }

        std::string sceneID = m_currentNode->getAutoTransitionForNode(type);
        if (sceneID.empty()) {
            return false;
        }

        Graph::Node* node = Graph::GraphTable::getNodeById(sceneID);
        if (!node) {
            return false;
        }

        ChangeNode(node);
        return true;
    }

    bool Thread::autoTransition(int index, std::string type) {
        if (!nodeQueue.empty()) {
            return false;
        }

        std::string sceneID = m_currentNode->getAutoTransitionForActor(index, type);
        if (sceneID.empty()) {
            return false;
        }

        Graph::Node* node = Graph::GraphTable::getNodeById(sceneID);
        if (!node) {
            return false;
        }

        ChangeNode(node);
        return true;
    }

    void Thread::warpTo(Graph::Node* node, bool useFades) {
        clearNodeQueue();

        if (playerThread && useFades) {
            std::thread fadeThread = std::thread([node] {
                GameAPI::GameCamera::fadeToBlack(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                Thread* thread = ThreadManager::GetSingleton()->getPlayerThread();
                if (thread) {
                    thread->ChangeNode(node);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(550));
                GameAPI::GameCamera::fadeFromBlack(1);
            });
            fadeThread.detach();
        } else {
            ChangeNode(node);
        }
    }

    void Thread::queueWarp(Graph::Node* node, int duration) {
        if (!inSequence) {
            warpTo(node, duration);
            nodeQueueCooldown = duration + 1000;
            inSequence = true;
            return;
        }

        if (!nodeQueue.empty() && nodeQueue.back().node == node) {
            nodeQueue.back().duration += duration;
            return;
        }

        nodeQueue.push({duration, node});
    }

    void Thread::navigateTo(Graph::Node* node, int duration) {
        if (!m_currentNode) {
            return;
        }

        clearNodeQueue();

        if (m_currentNode == node) {
            nodeQueueCooldown = duration;
            return;
        }

        std::vector<Graph::SequenceEntry> nodes = m_currentNode->getRoute(MCM::MCMTable::navigationDistanceMax(), getActorConditions(), node);
        if (nodes.empty()) {
            warpTo(node, MCM::MCMTable::useAutoModeFades() || node->fadeOnEntry);
            nodeQueueCooldown = duration + 1000;
        } else {
            nodes.back().duration = duration;
            for (int i = 1; i < nodes.size(); i++) {
                nodeQueue.push(nodes[i]);
            }
            nodeQueueCooldown = nodes.front().duration;
            if (playerThread && (MCM::MCMTable::useAutoModeFades() || nodes.front().node->fadeOnEntry)) {
                fadeAndChangeNode(nodes.front().node);
            } else {
                ChangeNode(nodes.front().node);
            }
        }
    }

    void Thread::queueNavigation(Graph::Node* node, int duration) {
        if (!inSequence) {
            navigateTo(node, duration);
            inSequence = true;
            return;
        }

        if (!nodeQueue.empty() && nodeQueue.back().node == node) {
            nodeQueue.back().duration += duration;
            return;
        }

        std::vector<Graph::SequenceEntry> nodes = (nodeQueue.empty() ? m_currentNode : nodeQueue.back().node)->getRoute(MCM::MCMTable::navigationDistanceMax(), getActorConditions(), node);
        if (nodes.empty()) {
            nodeQueue.push({duration, node});
        } else {
            nodes.back().duration = duration;
            for (Graph::SequenceEntry& entry : nodes) {
                nodeQueue.push(entry);
            }
        }
    }

    void Thread::playSequence(Graph::Sequence* sequence, bool navigateTo, bool useFades) {
        std::vector<Trait::ActorCondition> conditions = getActorConditions();
        if (!sequence->fulfilledBy(conditions)) {
            return;
        }

        clearNodeQueue();

        bool doWarp = false;
        inSequence = true;

        if (navigateTo && m_currentNode) {
            std::vector<Graph::SequenceEntry> nodes = m_currentNode->getRoute(MCM::MCMTable::navigationDistanceMax(), conditions, sequence->nodes.front().node);
            if (nodes.empty()) {
                doWarp = true;
            } else {
                for (int i = 1; i < nodes.size(); i++) {
                    nodeQueue.push(nodes[i]);
                }
                nodeQueueCooldown = nodes.front().duration;
                ChangeNode(nodes.front().node);
            }
        } else {
            doWarp = true;
        }
        

        if (doWarp) {
            if (useFades && playerThread) {
                std::thread fadeThread = std::thread([sequence] {
                    GameAPI::GameCamera::fadeToBlack(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(700));
                    Thread* thread = ThreadManager::GetSingleton()->getPlayerThread();
                    if (thread) {
                        for (int i = 1; i < sequence->nodes.size(); i++) {
                            thread->nodeQueue.push(sequence->nodes[i]);
                        }
                        thread->nodeQueueCooldown = sequence->nodes.front().duration;
                        thread->ChangeNode(sequence->nodes.front().node);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(550));
                    GameAPI::GameCamera::fadeFromBlack(1);
                });
                fadeThread.detach();
            } else {
                for (int i = 1; i < sequence->nodes.size(); i++) {
                    nodeQueue.push(sequence->nodes[i]);
                }
                nodeQueueCooldown = sequence->nodes.front().duration;
                ChangeNode(sequence->nodes.front().node);
            }
        } else {
            for (int i = 1; i < sequence->nodes.size(); i++) {
                nodeQueue.push(sequence->nodes[i]);
            }
        }
    }

    void Thread::playSequence(std::vector<Graph::SequenceEntry> nodes, bool navigateTo, bool useFades) {
        if (nodes.empty()) {
            return;
        }

        std::vector<Trait::ActorCondition> conditions = getActorConditions();
        for (Graph::SequenceEntry& entry : nodes) {
            if (!entry.node->fulfilledBy(conditions)) {
                return;
            }
        }

        clearNodeQueue();

        bool doWarp = false;
        inSequence = true;

        if (navigateTo && m_currentNode) {
            std::vector<Graph::SequenceEntry> path = m_currentNode->getRoute(MCM::MCMTable::navigationDistanceMax(), conditions, nodes.front().node);
            if (path.empty()) {
                doWarp = true;
            } else {
                for (int i = 1; i < path.size(); i++) {
                    nodeQueue.push(path[i]);
                }
                nodeQueueCooldown = path.front().duration;
                ChangeNode(path.front().node);
            }
        } else {
            doWarp = true;
        }
        

        if (doWarp) {
            if (useFades && playerThread) {
                std::thread fadeThread = std::thread([nodes] {
                    GameAPI::GameCamera::fadeToBlack(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(700));
                    Thread* thread = ThreadManager::GetSingleton()->getPlayerThread();
                    if (thread) {
                        for (int i = 1; i < nodes.size(); i++) {
                            thread->nodeQueue.push(nodes[i]);
                        }
                        thread->nodeQueueCooldown = nodes.front().duration;
                        thread->ChangeNode(nodes.front().node);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(550));
                    GameAPI::GameCamera::fadeFromBlack(1);
                });
                fadeThread.detach();
            } else {
                for (int i = 1; i < nodes.size(); i++) {
                    nodeQueue.push(nodes[i]);
                }
                nodeQueueCooldown = nodes.front().duration;
                ChangeNode(nodes.front().node);
            }
        } else {
            for (int i = 1; i < nodes.size(); i++) {
                nodeQueue.push(nodes[i]);
            }
        }
    }

    bool Thread::pullOut() {
        if (autoTransition("pullout")) {
            return true;
        }

        std::vector<std::function<bool(Graph::Node*)>> conditions;
        conditions.push_back([&](Graph::Node* node) { return node->findAnyAction({"analsex", "tribbing", "vaginalsex"}) == -1; });
        conditions.push_back([&](Graph::Node* node) { return node->findAction("malemasturbation") != -1; });

        Graph::Node* node = m_currentNode->getRandomNodeInRange(3, getActorConditions(), [&conditions](Graph::Node* node) { return checkConditions2(conditions, node); });

        if (node) {
            navigateTo(node);
            return true;
        }

        return false;
    }

    void Thread::clearNodeQueue() {
        if (nodeQueueCooldown == 0) {
            return;
        }

        nodeQueueCooldown = 0;
        while (!nodeQueue.empty()) {
            nodeQueue.pop();
        }

        if (inSequence) {
            if (endAfterSequence) {
                stop();
            } else {
                inSequence = false;
            }
        }
    }

    void Thread::loopNavigation() {
        if (nodeQueueCooldown > 0) {
            if ((nodeQueueCooldown -= Constants::LOOP_TIME_MILLISECONDS) <= 0) {
                if (nodeQueue.empty()) {
                    if (inSequence) {
                        if (endAfterSequence) {
                            stop();
                        } else {
                            inSequence = false;
                            UI::UIState::GetSingleton()->NodeChanged(this, m_currentNode);
                        }
                    }
                } else {
                    Graph::SequenceEntry next = nodeQueue.front();
                    nodeQueue.pop();
                    nodeQueueCooldown = next.duration;
                    ChangeNode(next.node);
                }
            }
        }
    }
}