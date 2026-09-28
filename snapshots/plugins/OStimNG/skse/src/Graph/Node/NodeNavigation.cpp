#include "Graph/Node.h"

#include "Util/VectorUtil.h"

namespace Graph {
    std::vector<Node*> Node::getNodesInRange(int distance, std::vector<Trait::ActorCondition> actorConditions, std::function<bool(Node*)> nodeCondition) {
        std::vector<Node*> nodes;
        std::vector<Node*> lastLevel = {this};
        std::vector<Node*> nextLevel;

        for (int i = 0; i < distance; i++) {
            for (Node* node : lastLevel) {
                for (Navigation nav : node->navigations) {
                    Node* dest = nav.nodes.back();
                    if (!VectorUtil::contains(nodes, dest) && nav.fulfilledBy(actorConditions)) {
                        nodes.push_back(dest);
                        nextLevel.push_back(dest);
                    }
                }
            }

            if (nextLevel.empty()) {
                break;
            }

            lastLevel = nextLevel;
            nextLevel.clear();
        }

        return nodes;
    }

    Node* Node::getRandomNodeInRange(int distance, std::vector<Trait::ActorCondition> actorConditions, std::function<bool(Node*)> nodeCondition) {
        std::vector<Node*> nodes = getNodesInRange(distance, actorConditions, nodeCondition);

        std::shuffle(std::begin(nodes), std::end(nodes), RNGUtil::RNG);

        for (auto& node : nodes) {
            if (!node->noRandomSelection && nodeCondition(node)) {
                return node;
            }
        }

        return nullptr;
    }

    std::vector<SequenceEntry> Node::getRoute(int distance, std::vector<Trait::ActorCondition> actorConditions, Node* destination) {
        std::vector<Node*> visited = {this};
        std::vector<std::pair<Node*, std::vector<Node*>>> routes = {{this, {}}};

        for (int i = 0; i < distance; i++) {
            std::vector<std::pair<Node*, std::vector<Node*>>> nextRoutes;
            for (std::pair<Node*, std::vector<Node*>> route : routes) {
                for (Navigation navigation : route.first->navigations) {
                    if (VectorUtil::contains(visited, navigation.nodes.back()) || !navigation.fulfilledBy(actorConditions)) {
                        continue;
                    }

                    visited.push_back(navigation.nodes.back());

                    std::vector<Node*> newRoute = route.second;
                    for (Node* node : navigation.nodes) {
                        newRoute.push_back(node);
                    }

                    if (navigation.nodes.back() == destination) {
                        std::vector<SequenceEntry> sequence;
                        for (Node* node : newRoute) {
                            sequence.push_back({node->isTransition ? node->animationLengthMs : 500, node});
                        }
                        return sequence;
                    }

                    nextRoutes.push_back({navigation.nodes.back(), newRoute});
                }
            }
            routes = nextRoutes;
            nextRoutes.clear();
        }

        return {};
    }
}