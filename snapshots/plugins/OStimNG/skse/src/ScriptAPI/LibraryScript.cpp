#include "LibraryScript.h"

#include "Graph/GraphTable.h"

namespace ScriptAPI {
    namespace Library {
        std::vector<std::string> getAllNodes() {
            std::vector<Graph::Node*> nodes = Graph::GraphTable::getNodes();

            std::sort(nodes.begin(), nodes.end(), [](Graph::Node* a, Graph::Node* b) { return a->lowercase_name < b->lowercase_name; });

            std::vector<std::string> ids;
            for (Graph::Node* node : nodes) {
                ids.push_back(node->scene_id);
            }

            return ids;
        }

        std::vector<std::string> getNodesInRange(std::string id, std::vector<GameAPI::GameActor> actors, int distance) {
            Graph::Node* node = Graph::GraphTable::getNodeById(id);

            if (!node) {
                return {};
            }

            std::vector<Trait::ActorCondition> actorConditions = Trait::ActorCondition::create(actors);
            std::vector<Graph::Node*> nodes = node->getNodesInRange(distance, actorConditions, [](Graph::Node* node) { return true; });

            std::sort(nodes.begin(), nodes.end(), [](Graph::Node* a, Graph::Node* b) { return a->lowercase_name < b->lowercase_name; });

            std::vector<std::string> ids;
            for (Graph::Node* node : nodes) {
                ids.push_back(node->scene_id);
            }

            return ids;
        }
    }
}