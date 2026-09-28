#include "Graph/GraphTable.h"

#include "Furniture/FurnitureTable.h"
#include "Game/LocaleManager.h"
#include "Util/JsonUtil.h"
#include "Util/StringUtil.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Graph {

    // defined in GraphTableSetupNodes.cpp
    extern const char* NODE_FILE_PATH;

    void GraphTable::reloadNode(std::string sceneId) {
        std::string lowercase = sceneId;
        StringUtil::toLower(&lowercase);

        auto it = nodes.find(lowercase);
        if (it == nodes.end()) {
            logger::warn("GraphTable::reloadNode: scene '{}' not found in graph table", sceneId);
            return;
        }

        // Find the file on disk
        std::string filePath;
        fs::path rootPath{NODE_FILE_PATH};
        if (fs::exists(rootPath)) {
            for (auto& entry : fs::recursive_directory_iterator{rootPath}) {
                const auto& p = entry.path();
                if (p.extension() == ".json" && p.stem().string() == sceneId) {
                    filePath = p.string();
                    break;
                }
            }
        }

        if (filePath.empty()) {
            logger::warn("GraphTable::reloadNode: file for scene '{}' not found on disk", sceneId);
            return;
        }

        std::ifstream ifs(filePath);
        json j = json::parse(ifs, nullptr, false);
        if (j.is_discarded()) {
            logger::warn("GraphTable::reloadNode: file for scene '{}' is malformed JSON", sceneId);
            return;
        }

        Node* node = it->second;

        // ── Remove old animation table entries ──────────────────────────────
        for (const auto& speed : node->speeds) {
            animationNodeTable.erase(speed.animation);
        }

        // ── Remove from nodeList ────────────────────────────────────────────
        auto furIt = nodeList.find(node->furnitureType->getMasterTypeInternal());
        if (furIt != nodeList.end()) {
            auto& innerMap = *furIt->second;
            int count = static_cast<int>(node->actors.size());
            auto vecIt = innerMap.find(count);
            if (vecIt != innerMap.end()) {
                auto& vec = *vecIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), node), vec.end());
            }
        }

        // ── Reset mutable fields in-place (pointer stays valid for threads) ─
        node->scene_name          = "";
        node->lowercase_name      = "";
        node->modpack             = "";
        node->animationLengthMs   = 0;
        node->isTransition        = false;
        node->hasIdleSpeed        = false;
        node->noRandomSelection   = false;
        node->defaultSpeed        = 0;
        node->scaleOffsetWithFurniture = false;
        node->furnitureType       = Furniture::FurnitureTable::getFurnitureType("none");
        node->autoTransitions.clear();
        node->tags.clear();
        node->actors.clear();
        node->actions.clear();
        node->navigations.clear();
        node->speeds.clear();
        node->offset              = {};

        // ── Re-populate from JSON (mirrors GraphTableSetupNodes logic) ──────
        if (j.contains("name")) {
            if (j["name"].is_string()) {
                node->lowercase_name = node->scene_name = LocaleManager::GetSingleton()->GetLocalization(static_cast<std::string>(j["name"]));
                StringUtil::toLower(&node->lowercase_name);
            } else {
                logger::warn("name property of scene {} isn't a string", sceneId);
            }
        } else {
            node->lowercase_name = node->scene_name = LocaleManager::GetSingleton()->GetLocalization("$" + sceneId);
            StringUtil::toLower(&node->lowercase_name);
        }

        if (j.contains("modpack") && j["modpack"].is_string()) {
            node->modpack = j["modpack"];
        }

        if (j.contains("length")) {
            if (j["length"].is_number()) {
                node->animationLengthMs = static_cast<int>(static_cast<float>(j["length"]) * 1000.0f);
            } else {
                logger::warn("length property of scene {} isn't a number", sceneId);
            }
        } else {
            logger::warn("scene {} doesn't have a length property defined", sceneId);
        }

        std::vector<Graph::RawNavigation> navigations;

        if (j.contains("destination")) {
            if (j["destination"].is_string()) {
                node->isTransition = true;
                navigations.push_back({.origin = sceneId, .destination = j["destination"]});
                if (j.contains("origin") && j["origin"].is_string()) {
                    RawNavigation nav = {.origin = j["origin"], .destination = sceneId};
                    if (j.contains("priority") && j["priority"].is_number_integer()) nav.priority = j["priority"];
                    if (j.contains("description") && j["description"].is_string()) nav.description = LocaleManager::GetSingleton()->GetLocalization(static_cast<std::string>(j["description"]));
                    if (j.contains("icon") && j["icon"].is_string()) nav.icon = j["icon"];
                    if (j.contains("border") && j["border"].is_string()) nav.border = j["border"];
                    if (j.contains("noWarnings") && j["noWarnings"].is_boolean()) nav.noWarnings = j["noWarnings"];
                    navigations.push_back(nav);
                }
            }
        } else if (j.contains("navigations") && j["navigations"].is_array()) {
            int index = 0;
            for (auto& jsonNav : j["navigations"]) {
                if (jsonNav.is_object()) {
                    RawNavigation nav = {.origin = sceneId, .destination = sceneId};
                    if (jsonNav.contains("destination") && jsonNav["destination"].is_string()) nav.destination = jsonNav["destination"];
                    if (jsonNav.contains("origin") && jsonNav["origin"].is_string()) nav.origin = jsonNav["origin"];
                    if (jsonNav.contains("priority") && jsonNav["priority"].is_number_integer()) nav.priority = jsonNav["priority"];
                    if (jsonNav.contains("description") && jsonNav["description"].is_string()) nav.description = LocaleManager::GetSingleton()->GetLocalization(static_cast<std::string>(jsonNav["description"]));
                    if (jsonNav.contains("icon") && jsonNav["icon"].is_string()) nav.icon = jsonNav["icon"];
                    if (jsonNav.contains("border") && jsonNav["border"].is_string()) nav.border = jsonNav["border"];
                    if (jsonNav.contains("noWarnings") && jsonNav["noWarnings"].is_boolean()) nav.noWarnings = jsonNav["noWarnings"];
                    if (nav.origin != nav.destination) navigations.push_back(nav);
                }
                index++;
            }
        }

        if (j.contains("speeds") && j["speeds"].is_array()) {
            int index = 0;
            for (auto& jsonSpeed : j["speeds"]) {
                if (jsonSpeed.is_object()) {
                    Speed speed;
                    if (jsonSpeed.contains("animation") && jsonSpeed["animation"].is_string()) speed.animation = jsonSpeed["animation"];
                    if (jsonSpeed.contains("playbackSpeed") && jsonSpeed["playbackSpeed"].is_number()) speed.playbackSpeed = jsonSpeed["playbackSpeed"];
                    if (jsonSpeed.contains("displaySpeed") && jsonSpeed["displaySpeed"].is_number()) speed.displaySpeed = jsonSpeed["displaySpeed"];
                    node->speeds.push_back(speed);
                }
                index++;
            }
        }

        if (j.contains("defaultSpeed") && j["defaultSpeed"].is_number_integer()) node->defaultSpeed = j["defaultSpeed"];
        if (j.contains("noRandomSelection") && j["noRandomSelection"].is_boolean()) node->noRandomSelection = j["noRandomSelection"];

        if (j.contains("furniture")) {
            if (j["furniture"].is_string()) {
                node->furnitureType = Furniture::FurnitureTable::getFurnitureType(j["furniture"]);
                if (node->furnitureType->id == "none") logger::warn("furniture type of scene {} doesn't exist", sceneId);
            } else {
                logger::warn("furniture property of scene {} isn't a string", sceneId);
            }
        }

        if (j.contains("offset")) node->offset.loadJson(filePath, j["offset"]);
        JsonUtil::loadBool(j, node->scaleOffsetWithFurniture, "scaleOffsetWithFurniture", sceneId, "scene", false);

        if (j.contains("tags") && j["tags"].is_array()) {
            for (auto& jsonTag : j["tags"]) {
                if (jsonTag.is_string()) {
                    std::string tag = jsonTag;
                    StringUtil::toLower(&tag);
                    node->tryAddTag(tag);
                }
            }
        }

        if (j.contains("autoTransitions") && j["autoTransitions"].is_object()) {
            for (auto& [id, transition] : j["autoTransitions"].items()) {
                if (transition.is_string()) node->autoTransitions[id] = transition;
            }
        }

        if (j.contains("actors") && j["actors"].is_array()) {
            int index = 0;
            for (auto& jsonActor : j["actors"]) {
                Graph::GraphActor actor;
                if (jsonActor.is_object()) {
                    std::string objectType = "actor " + std::to_string(index) + " of scene";
                    JsonUtil::loadLowerString(jsonActor, actor.condition.type, "type", sceneId, objectType, false);
                    JsonUtil::consumeString(jsonActor, [&actor](std::string sex) { actor.condition.sex = GameAPI::GameSexAPI::fromString(sex); }, "intendedSex", sceneId, objectType, false);
                    JsonUtil::loadInt(jsonActor, actor.sosBend, "sosBend", sceneId, objectType, false);
                    JsonUtil::loadInt(jsonActor, actor.sosBend, "tngBend", sceneId, objectType, false);
                    JsonUtil::loadFloat(jsonActor, actor.scale, "scale", sceneId, objectType, false);
                    JsonUtil::loadFloat(jsonActor, actor.scaleHeight, "scaleHeight", sceneId, objectType, false);
                    JsonUtil::loadString(jsonActor, actor.underlyingExpression, "underlyingExpression", sceneId, objectType, false);
                    JsonUtil::loadString(jsonActor, actor.expressionOverride, "expressionOverride", sceneId, objectType, false);
                    JsonUtil::loadInt(jsonActor, actor.expressionAction, "expressionAction", sceneId, objectType, false);
                    actor.animationIndex = index;
                    JsonUtil::loadInt(jsonActor, actor.animationIndex, "animationIndex", sceneId, objectType, false);
                    JsonUtil::loadBool(jsonActor, actor.noStrip, "noStrip", sceneId, objectType, false);

                    if (jsonActor.contains("lookUp") && jsonActor["lookUp"].is_number_integer()) {
                        int value = jsonActor["lookUp"]; int type = value < 0 ? (value *= -1, 8) : 11;
                        actor.eyeballModifierOverride[type] = {.type = type, .baseValue = static_cast<float>(value)};
                    } else if (jsonActor.contains("lookDown") && jsonActor["lookDown"].is_number_integer()) {
                        int value = jsonActor["lookDown"]; int type = value < 0 ? (value *= -1, 11) : 8;
                        actor.eyeballModifierOverride[type] = {.type = type, .baseValue = static_cast<float>(value)};
                    }
                    if (jsonActor.contains("lookLeft") && jsonActor["lookLeft"].is_number_integer()) {
                        int value = jsonActor["lookLeft"]; int type = value < 0 ? (value *= -1, 10) : 9;
                        actor.eyeballModifierOverride[type] = {.type = type, .baseValue = static_cast<float>(value)};
                    } else if (jsonActor.contains("lookRight") && jsonActor["lookRight"].is_number_integer()) {
                        int value = jsonActor["lookRight"]; int type = value < 0 ? (value *= -1, 9) : 10;
                        actor.eyeballModifierOverride[type] = {.type = type, .baseValue = static_cast<float>(value)};
                    }
                    if (jsonActor.contains("offset")) actor.offset.loadJson(filePath, jsonActor["offset"]);

                    JsonUtil::consumeLowerStringList(jsonActor, [&actor](std::string req) { actor.condition.requirements.insert(req); }, "requirements", sceneId, objectType, false);
                    std::vector<std::string> tags;
                    JsonUtil::loadLowerStringList(jsonActor, tags, "tags", sceneId, objectType, false);
                    for (std::string tag : tags) { GraphActorTag t; t.tag = tag; actor.tags.push_back(t); }
                    actor.feetOnGround = actor.hasAnyTag({"standing", "squatting"});
                    JsonUtil::loadBool(jsonActor, actor.feetOnGround, "feetOnGround", sceneId, objectType, false);
                    JsonUtil::consumeLowerStringMap(jsonActor, [&actor](std::string id, std::string transition) { actor.autoTransitions[id] = transition; }, true, "autoTransitions", sceneId, objectType, false);
                }
                node->actors.push_back(actor);
                index++;
            }
        }

        if (j.contains("actions") && j["actions"].is_array()) {
            int index = 0;
            for (auto& jsonAction : j["actions"]) {
                Graph::Action::Action action;
                action.index = index;
                if (jsonAction.is_object()) {
                    std::string objectType = "action " + std::to_string(index) + " of node";
                    JsonUtil::consumeString(jsonAction, [&action](std::string type) { action.type = getActionAlias(type); }, "type", sceneId, objectType, true);
                    RoleMapAPI::KEYS.forEach([&jsonAction, &action, index, &sceneId](Role role, std::string key) {
                        if (jsonAction.contains(key)) {
                            if (jsonAction[key].is_number_integer()) *action.roles.get(role) = jsonAction[key];
                        } else {
                            if (role != Role::ACTOR) *action.roles.get(role) = *action.roles.get(Role::ACTOR);
                        }
                    });
                    JsonUtil::loadBool(jsonAction, action.muted, "muted", sceneId, objectType, false);
                    JsonUtil::loadBool(jsonAction, action.doPeaks, "doPeaks", sceneId, objectType, false);
                    JsonUtil::loadBool(jsonAction, action.peaksAnnotated, "peaksAnnotated", sceneId, objectType, false);
                }
                action.attributes = getActionAttributesByType(action.type);
                node->actions.push_back(action);
                index++;
            }
        }

        if (node->isTransition) node->tryAddTag("transition");

        if (node->actors.size() >= 2) {
            bool gay = true, lesbian = true;
            for (GraphActor actor : node->actors) {
                if (actor.condition.sex != GameAPI::GameSex::MALE)   gay     = false;
                if (actor.condition.sex != GameAPI::GameSex::FEMALE) lesbian = false;
                if (!gay && !lesbian) break;
            }
            if (gay) node->tryAddTag("gay");
            else if (lesbian) node->tryAddTag("lesbian");
        }

        node->mergeNodeIntoActors();

        // ── Re-add to animation table and nodeList ──────────────────────────
        for (const Graph::Speed& speed : node->speeds) {
            animationNodeTable.insert({speed.animation, node});
        }

        {
            std::unordered_map<int, std::vector<Node*>*>* innerMap;
            int count = static_cast<int>(node->actors.size());
            auto furIt2 = nodeList.find(node->furnitureType->getMasterTypeInternal());
            if (furIt2 != nodeList.end()) {
                innerMap = furIt2->second;
            } else {
                innerMap = new std::unordered_map<int, std::vector<Node*>*>();
                nodeList.insert({node->furnitureType->getMasterTypeInternal(), innerMap});
            }
            auto vecIt2 = innerMap->find(count);
            if (vecIt2 != innerMap->end()) {
                vecIt2->second->push_back(node);
            } else {
                auto* vec = new std::vector<Node*>();
                vec->push_back(node);
                innerMap->insert({count, vec});
            }
        }

        // ── Remove stale navigations in other nodes where this node is an intermediate
        //    transition hop. Navigations where this node is the final destination are
        //    owned by those other scenes' JSON files and must not be touched.
        for (auto& [id, otherNode] : nodes) {
            if (otherNode == node) continue;
            otherNode->navigations.erase(
                std::remove_if(otherNode->navigations.begin(), otherNode->navigations.end(),
                    [node](const Navigation& nav) {
                        // nav.nodes.back() is the final destination – skip it
                        for (size_t i = 0; i + 1 < nav.nodes.size(); ++i) {
                            if (nav.nodes[i] == node) return true;
                        }
                        return false;
                    }),
                otherNode->navigations.end());
        }

        // ── Re-add navigations for this node ────────────────────────────────
        if (!navigations.empty()) {
            std::stable_sort(navigations.begin(), navigations.end(), [](const RawNavigation& a, const RawNavigation& b) {
                return a.priority < b.priority;
            });
            addNavigations(navigations);
        }

        logger::info("GraphTable::reloadNode: hot-reloaded scene '{}'", sceneId);
    }

}
