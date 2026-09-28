#include "Graph/GraphTable.h"

#include "Util/Constants.h"
#include "Util/LegacyUtil.h"
#include "Util/RNGUtil.h"
#include "Util/StringUtil.h"
#include <Util/UIUtil.h>

namespace Graph {
    void GraphTable::addNode(Node* node) {
        nodes[node->lowercase_id] = node;
        for (Graph::Speed speed : node->speeds) {
            animationNodeTable.insert({speed.animation, node});
        }

        std::unordered_map<int, std::vector<Node*>*>* innerMap;
        std::vector<Node*>* innerList;
        int count = node->actors.size();
        auto iter = nodeList.find(node->furnitureType->getMasterTypeInternal());
        if (iter != nodeList.end()) {
            innerMap = iter->second;
        } else {
            innerMap = new std::unordered_map<int, std::vector<Node*>*>();
            nodeList.insert({node->furnitureType->getMasterTypeInternal(), innerMap});
        }

        auto iter2 = innerMap->find(count);
        if (iter2 != innerMap->end()) {
            innerList = iter2->second;
        } else {
            innerList = new std::vector<Node*>();
            innerMap->insert({count, innerList});
        }
        
        innerList->push_back(node);
    }

    void GraphTable::addNavigations(std::vector<RawNavigation> navigations) {
        for (RawNavigation& raw : navigations) {
            Node* start = getNodeById(raw.origin);
            if (!start) {
                if (!raw.noWarnings) {
                    logger::warn("Couldn't add navigation from {} to {} because {} doesn't exist.", raw.origin, raw.destination, raw.origin);
                }
                continue;
            }

            Node* destination = getNodeById(raw.destination);
            if (!destination) {
                if (!raw.noWarnings) {
                    logger::warn("Couldn't add navigation from {} to {} because {} doesn't exist.", raw.origin, raw.destination, raw.destination);
                }
                continue;
            }

            if (!start->furnitureType->isChildOf(destination->furnitureType) && !destination->furnitureType->isChildOf(start->furnitureType)) {
                logger::warn("Couldn't add navigation from {} to {} because their furniture types aren't compatible.", raw.origin, raw.destination);
                continue;
            }

            if (start->actors.size() != destination->actors.size()) {
                logger::warn("Couldn't add navigation from {} to {} because their actor counts don't match.", raw.origin, raw.destination);
                continue;
            }

            if (!start->hasSameActorTpyes(destination)) {
                logger::warn("Couldn't add navigation from {} to {} because their actor types don't match.", raw.origin, raw.destination);
            }

            bool alreadyExists = false;
            for (auto& existingNavigation : start->navigations) {
                if (existingNavigation.nodes.back() == destination || existingNavigation.nodes.front() == destination) {
                    alreadyExists = true;
                    break;
                }
            }
            if (alreadyExists) {
                continue;
            }

            Navigation navigation;

            Graph::Node* currentNode = destination;

            while (currentNode && currentNode->isTransition) {
                navigation.nodes.push_back(currentNode);
                Graph::Node* next = nullptr;
                for (RawNavigation& nav : navigations) {
                    if (nav.origin == currentNode->scene_id) {
                        next = GraphTable::getNodeById(nav.destination);
                        break;
                    }
                }

                // Fallback for partial-reload: the transition's destination isn't in the
                // local navigations vector (only the reloaded node's navs are present),
                // so use the transition node's already-built navigation chain instead.
                if (!next && !currentNode->navigations.empty()) {
                    next = currentNode->navigations.front().nodes.front();
                }

                if (!next) {
                    logger::warn("Couldn't add navigation from {} to {} because {} transition destination doesn't exist.", raw.origin, raw.destination, currentNode->scene_id);
                }

                currentNode = next;
            }

            if (!currentNode) {
                continue;
            }

            navigation.nodes.push_back(currentNode);

            if (raw.description.empty()) {
                navigation.description = navigation.isTransition && navigation.nodes.back() == start ? navigation.nodes.front()->scene_id : navigation.nodes.back()->scene_id;
            } else {
                navigation.description = raw.description;
            }

            if (std::regex_search(raw.border, Constants::hexColor)) {
                navigation.border = raw.border;
            }
            if (raw.icon == "") {
                navigation.icon = UI::getIconTexture(LegacyUtil::getIcon(navigation.nodes.back() == start ? navigation.nodes.front() : navigation.nodes.back()));
            } else {
                navigation.icon = UI::getIconTexture(raw.icon);
            }

            start->navigations.push_back(navigation);
        }
    }

    std::vector<Node*> GraphTable::getNodes() {
        std::vector<Node*> nodes;

        for (std::unordered_map<std::string, Node*>::iterator it = GraphTable::nodes.begin(); it != GraphTable::nodes.end(); it++) {
            nodes.push_back(it->second);
        }

        return nodes;
    }

    Node* GraphTable::getNodeById(std::string id) {
        StringUtil::toLower(&id);
        auto iter = nodes.find(id);
        if (iter != nodes.end()) {
            return iter->second;
        }
        return nullptr;
    }

    bool ContainsName(Node* n, std::string& name) {
        if (n->lowercase_name.contains(name)) return true;
        return false;
    }

    void GraphTable::searchNodesByName(std::string& name, std::vector<Node*>& results) {
        StringUtil::toLower(&name);
        for (auto& iter : nodes) {
            if (ContainsName(iter.second, name)) {
                results.push_back(iter.second);
            }
        }
    }

    Node* GraphTable::getNodeByAnimation(std::string anim) {
        auto iter = animationNodeTable.find(anim);
        if (iter != animationNodeTable.end()) {
            return iter->second;
        }
        return nullptr;
    }

    bool GraphTable::hasNodes(Furniture::FurnitureType* furnitureType, int actorCount) {
        furnitureType = furnitureType->getMasterTypeInternal();

        auto iter = nodeList.find(furnitureType);
        if (iter == nodeList.end()) {
            return false;
        }

        auto iter2 = iter->second->find(actorCount);
        return iter2 != iter->second->end();
    }

    Node* GraphTable::getRandomNode(Furniture::FurnitureType* furnitureType, std::vector<Trait::ActorCondition> actorConditions, std::function<bool(Node*)> nodeCondition) {
        Furniture::FurnitureType* masterType = furnitureType->getMasterTypeInternal();

        auto iter = nodeList.find(masterType);
        if (iter == nodeList.end()) {
            return nullptr;
        }
        auto innerMap = iter->second;
        
        auto iter2 = innerMap->find(actorConditions.size());
        if (iter2 == innerMap->end()) {
            return nullptr;
        }
        // the copy is to prevent race conditions if several scripts try to call this at once
        std::vector<Node*> copy = *iter2->second;

        std::shuffle(std::begin(copy), std::end(copy), RNGUtil::RNG);

        for (Node* node : copy) {
            if (!node->isTransition && !node->noRandomSelection && furnitureType->isChildOf(node->furnitureType) && node->fulfilledBy(actorConditions) && (!nodeCondition || nodeCondition(node))) {
                return node;
            }
        }
        
        return nullptr;
    }

}