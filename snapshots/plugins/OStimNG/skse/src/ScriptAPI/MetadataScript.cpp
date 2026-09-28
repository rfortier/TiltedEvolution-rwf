#include "MetadataScript.h"

#include "Game/LocaleManager.h"
#include "Graph/GraphTable.h"

namespace ScriptAPI {
    namespace Metadata {
        std::string getName(std::string id) {
            Graph::Node* node = Graph::GraphTable::getNodeById(id);

            return node ? node->scene_name : "";
        }

        std::vector<std::string> nodesToNames(std::vector<std::string> ids) {
            std::vector<std::string> names;

            for (std::string id : ids) {
                Graph::Node* node = Graph::GraphTable::getNodeById(id);
                names.push_back(node ? node->scene_name : "");
            }

            return names;
        }
    }
}