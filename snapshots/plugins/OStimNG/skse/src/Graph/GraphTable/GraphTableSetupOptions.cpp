#include "Graph/GraphTable.h"
#include "UI/Scene/SceneOptions.h"
#include <Util/JsonFileLoader.h>
#include <Util.h>
#include <Util/UIUtil.h>
#include <functional>


namespace Graph {
    const char* OPTION_FILE_PATH{ "Data/SKSE/Plugins/OStim/options" };

    void GraphTable::setupOptions() {
        std::unordered_map<std::string, MenuNode> rawPages;
        std::vector<OptionNode> options;
        Util::JsonFileLoader::LoadFilesInSubfolders(OPTION_FILE_PATH, [&rawPages, &options](std::string path, std::string filename, json json) {
            if (json.contains("pages")) {

                if (json["pages"].is_array()) {
                    for (auto& page : json["pages"]) {
                        MenuNode node;
                        if (page.contains("name")) {

                            if (page["name"].is_string()) {
                                node.name = page["name"];
                            }
                            if (page.contains("parent")) {
                                if (page["parent"].is_null()) {
                                    node.parentName = "";
                                }
                                else if (page["parent"].is_string()) {
                                    node.parentName = page["parent"];
                                }
                            }
                            else {
                                logger::warn("page must contain parent (empty or null string) {} {}", path, node.name);
                            }
                            if (page.contains("description") && page["description"].is_string()) {
                                node.description = page["description"];
                            }
                            else {
                                node.description = node.name;
                            }
                            if (page.contains("icon") && page["icon"].is_string()) {
                                node.icon = UI::getIconTexture(page["icon"]);
                            }
                            if (page.contains("border") && page["border"].is_string()) {
                                node.border = page["border"];
                            }

                            if (page.contains("repeat") && page["repeat"].is_string()) {
                                node.repeat = page["repeat"];
                                if (page.contains("equipType") && page["equipType"].is_string()) {
                                    node.equipType = page["equipType"];
                                }
                            }
                        }
                        else {
                            logger::warn("page doesnt contain name {}", path);
                        }

                        if (rawPages.find(node.name) == rawPages.end()) {
                            rawPages.insert(std::make_pair(node.name, node));
                        }
                    }

                }
                else {
                    logger::warn("pages is not an array {}", path);
                }

            }
            if (json.contains("options")) {
                if (json["options"].is_array()) {
                    auto index = 0;
                    for (auto& option : json["options"]) {
                        OptionNode node;
                        if (!option.contains("name"))
                        {
                            logger::warn("option contains no name {} {}", path, index);
                            continue;
                        }
                        if (option["name"].is_string()) {
                            node.name = option["name"];
                        }
                        if (!option.contains("page")) {
                            logger::warn("option doesn't contain page {} {}", path, node.name);
                            continue;
                        }
                        if (option["page"].is_string()) {
                            node.parentName = option["page"];
                        }
                        else {
                            logger::warn("option->page is not string, {} {}", path, node.name);
                        }
                        
                        if (option.contains("stateFaction") && option["stateFaction"].is_string()) {
                            auto split = stl::string_split(option["stateFaction"].get<std::string>(), ';');
                            if (split.size() != 2) {
                                logger::warn("state faction not valid {}", node.parentName);
                                continue;
                            }
                            node.stateFaction = option["stateFaction"].get<std::string>();
                        }
                        if (option.contains("repeat") && option["repeat"].is_string()) {
                            node.repeat = option["repeat"];
                            if (option.contains("equipType") && option["equipType"].is_string()) {
                                node.equipType = option["equipType"];
                            }
                        }

                        if (!option.contains("states")) {
                            logger::warn("option contains no states {}", node.name);
                        }
                        else {
                            auto states = option["states"];
                            for (json::iterator it = states.begin(); it != states.end(); ++it) {
                                StateData data;
                                if (it.value().contains("description") && it.value()["description"].is_string()) {
                                    data.description = it.value()["description"];
                                }
                                if (it.value().contains("icon") && it.value()["icon"].is_string()) {
                                    data.icon = UI::getIconTexture(it.value()["icon"]);
                                }
                                if (it.value().contains("border") && it.value()["border"].is_string()) {
                                    data.border = it.value()["border"];
                                }
                                if (it.value().contains("function") && it.value()["function"].is_string()) {
                                    auto fn = it.value()["function"].get<std::string>();
                                    auto split = stl::string_split(fn, '/');
                                    if (split.size() != 2) {
                                        logger::warn("function not valid {} {}", node.name, it.key());
                                        continue;
                                    }
                                    data.script = split[0];
                                    data.function = split[1];
                                }
                                node.states.insert(std::make_pair(it.key(), data));
                            }
                        }
                        options.push_back(node);
                        index++;
                    }
                }
                else {
                    logger::warn("options is not an array {}", path);
                }
            }
        });
        
        
        for (auto& rawPage : rawPages) {
            std::vector<MenuNode*> heirarchy;
            constructHeirarchy(&rawPage.second, heirarchy, rawPages);

            for (int i = 0; i <  heirarchy.size(); i++) {

                if (heirarchy[i] != nullptr) {
                    if (i == 0) {
                        bool found = false;
                        for (auto& root : rootNode.subNodes) {
                            if (root.name == heirarchy[i]->name) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            heirarchy[i]->parent = nullptr;
                            rootNode.subNodes.push_back(*heirarchy[i]);
                        }
                    }
                    else {
                        if (auto parent = findParent(heirarchy[i], &rootNode.subNodes); parent != nullptr) {
                            parent->subNodes.push_back(*heirarchy[i]);
                            parent->subNodes[parent->subNodes.size() - 1].parent = parent;
                        }
                    }
                }
            }
        }

        for (auto& option : options) {
            auto parent = findParent(&option, &rootNode.subNodes);
            if (parent == nullptr) {
                logger::warn("cant find parent {} for {}", option.parentName, option.name);
                continue;
            }
            else { 
                parent->options.push_back(option);
                parent->options[parent->options.size() - 1].parent = parent;
            }
            if (option.stateFaction != "") {
                logger::info("{}", option.stateFaction);
                if (optionFactions.find(option.stateFaction) == optionFactions.end()) {

                    auto split = stl::string_split(option.stateFaction, ';');

                    GameAPI::GameFaction faction;
                    faction.loadFile(split[0], std::stoi(split[1], nullptr, 16));
                    logger::info("{}", faction.form->GetFormEditorID());
                    optionFactions.insert(std::make_pair(option.stateFaction, faction));
                }
            }
        }


    }

    MenuNode* GraphTable::findParent(Graph::MenuNode* findingNode, std::vector<Graph::MenuNode>* options) {
        Graph::MenuNode* parent = nullptr;
        for (auto& node : *options) {
            if (node.name == findingNode->parentName) { //Found the parent
                for (auto& child : node.subNodes) {
                    if (child.name == findingNode->name) {
                        return nullptr;
                    }
                }
                return &node;
            }
            else { //Check the children
                if (parent == nullptr)
                    parent = findParent(findingNode, &node.subNodes);
            }
        }
        return parent;
    }

    MenuNode* GraphTable::constructHeirarchy(MenuNode* leaf, std::vector<MenuNode*>& heirarchy, std::unordered_map<std::string, MenuNode>& rawPages) {
        if (auto search = rawPages.find(leaf->name); search == rawPages.end()) {
            return nullptr;
        }
        if (leaf->parentName == ""){            
                heirarchy.push_back(leaf);
                return leaf;            
        }
        
        if (auto parent = rawPages.find(leaf->parentName); parent != rawPages.end()) {
            auto result = constructHeirarchy(
                &parent->second,
                heirarchy,
                rawPages
            );
            if (result != nullptr) {
                heirarchy.push_back(leaf);
                return leaf;
            }
            else {
                logger::warn("Parent {} not found", leaf->parentName);
                return nullptr;
            }
        }
        else {
            logger::warn("Parent {} not found", leaf->parentName);
            return nullptr;
        }           
        
    }
}

