#include "FurnitureTable.h"

#include "Core/ThreadManager.h"
#include "GameAPI/GameList.h"
#include "Util/JsonFileLoader.h"
#include "Util/JsonUtil.h"
#include "Util/StringUtil.h"

namespace Furniture {
    const char* FURNITURE_TYPE_FILE_PATH{"Data/SKSE/Plugins/OStim/furniture types"};

    void FurnitureTable::setupFurnitureTypes() {
        std::unordered_map<std::string, std::string> rawSuperTypes;

        Util::JsonFileLoader::LoadFilesInSubfolders(FURNITURE_TYPE_FILE_PATH, [&rawSuperTypes](std::string path, std::string filename, json json) {
            FurnitureType furnitureType(filename);
            StringUtil::toLower(&furnitureType.id);

            JsonUtil::loadTranslatedString(json, furnitureType.name, "name", filename, "furniture type", false);
            JsonUtil::loadInt(json, furnitureType.priority, "priority", filename, "furniture type", false);
            JsonUtil::loadBool(json, furnitureType.listIndividually, "listIndividually", filename, "furniture type", false);
            JsonUtil::loadBool(json, furnitureType.ignoreMarkerOffsetX, "ignoreMarkerOffsetX", filename, "furniture type", false);
            JsonUtil::loadBool(json, furnitureType.ignoreMarkerOffsetY, "ignoreMarkerOffsetY", filename, "furniture type", false);
            JsonUtil::loadBool(json, furnitureType.ignoreMarkerOffsetZ, "ignoreMarkerOffsetZ", filename, "furniture type", false);
            JsonUtil::loadFloat(json, furnitureType.offsetX, "offsetX", filename, "furniture type", false);
            JsonUtil::loadFloat(json, furnitureType.offsetY, "offsetY", filename, "furniture type", false);
            JsonUtil::loadFloat(json, furnitureType.offsetZ, "offsetZ", filename, "furniture type", false);
            JsonUtil::loadFloat(json, furnitureType.rotation, "rotation", filename, "furniture type", false);
            JsonUtil::loadFloat(json, furnitureType.multiplyScale, "multiplyScale", filename, "furniture type", false);
            JsonUtil::loadGameRecord(json, furnitureType.offsetXGlobal, "offsetXGlobal", filename, "furniture type", path, false);
            JsonUtil::loadGameRecord(json, furnitureType.offsetYGlobal, "offsetYGlobal", filename, "furniture type", path, false);
            JsonUtil::loadGameRecord(json, furnitureType.offsetZGlobal, "offsetZGlobal", filename, "furniture type", path, false);

            if (json.contains("faction")) {
                if (json["faction"].is_array()) {
                    for (auto& jsonFaction : json["faction"]) {
                        GameAPI::GameFaction faction;
                        faction.loadJson(path, jsonFaction);
                        if (faction) {
                            furnitureType.factions.push_back(faction);
                        }
                    }
                } else {
                    GameAPI::GameFaction faction;
                    faction.loadJson(path, json["faction"]);
                    if (faction) {
                        furnitureType.factions.push_back(faction);
                    }
                }
            }

            if (json.contains("supertype")) {
                if (json["supertype"].is_string()) {
                    std::string supertype = json["supertype"];
                    StringUtil::toLower(&supertype);
                    rawSuperTypes[furnitureType.id] = supertype;
                } else {
                    logger::warn("property 'supertype' of furniture type {} isn't a string", filename);
                }
            }

            if (json.contains("conditions")) {
                if (json["conditions"].is_array()) {
                    std::vector<std::function<bool(GameAPI::GameObject)>> conditions;
                    int index = 0;
                    for (auto& jsonCondition : json["conditions"]) {
                        std::string type;
                        JsonUtil::loadString(jsonCondition, type, "type", filename, "condition " + std::to_string(index) + " of furniture type", true);
                        if (type.empty()) {
                            index++;
                            continue;
                        }

                        if (type == "anykeyword") {
                            if (jsonCondition.contains("keywords")) {
                                if (jsonCondition["keywords"].is_array()) {
                                    std::vector<GameAPI::GameKeyword> keywords;
                                    for (auto& jsonKeyword : jsonCondition["keywords"]) {
                                        GameAPI::GameKeyword keyword;
                                        keyword.loadJson(path, jsonKeyword);
                                        if (keyword) {
                                            keywords.push_back(keyword);
                                        }
                                    }

                                    if (keywords.empty()) {
                                        logger::warn("keyword list of condition {} of furniture type {} is empty", index, filename);
                                    } else {
                                        conditions.push_back([keywords](GameAPI::GameObject object) {
                                            for (GameAPI::GameKeyword keyword : keywords) {
                                                if (object.hasKeyword(keyword)) {
                                                    return true;
                                                }
                                            }    
                                            return false;
                                        });
                                    }
                                } else {
                                    logger::warn("property 'keywords' of condition {} of furniture type {} isn't a list", index, filename);
                                }
                            } else {
                                logger::warn("condition {} of furniture type {} doesn't have property 'keywords' defined", index, filename);
                            }
                        } else if (type == "keywordblacklist") {
                            if (jsonCondition.contains("keywords")) {
                                if (jsonCondition["keywords"].is_array()) {
                                    std::vector<GameAPI::GameKeyword> keywords;
                                    for (auto& jsonKeyword : jsonCondition["keywords"]) {
                                        GameAPI::GameKeyword keyword;
                                        keyword.loadJson(path, jsonKeyword);
                                        if (keyword) {
                                            keywords.push_back(keyword);
                                        }
                                    }

                                    if (keywords.empty()) {
                                        logger::warn("keyword list of condition {} of furniture type {} is empty", index, filename);
                                    } else {
                                        conditions.push_back([keywords](GameAPI::GameObject object) {
                                            for (GameAPI::GameKeyword keyword : keywords) {
                                                if (object.hasKeyword(keyword)) {
                                                    return false;
                                                }
                                            }    
                                            return true;
                                        });
                                    }
                                } else {
                                    logger::warn("property 'keywords' of condition {} of furniture type {} isn't a list", index, filename);
                                }
                            } else {
                                logger::warn("condition {} of furniture type {} doesn't have property 'keywords' defined", index, filename);
                            }
                        } else if (type == "formlist") {
                            GameAPI::GameList list;
                            JsonUtil::loadGameRecord(jsonCondition, list, "formlist", filename, "condition " + std::to_string(index) + " of furniture type", path, true);
                            if (list) {
                                conditions.push_back([list](GameAPI::GameObject object) { return list.contains(object); });
                            }
                        } else if (type == "formblacklist") {
                            GameAPI::GameList list;
                            JsonUtil::loadGameRecord(jsonCondition, list, "formlist", filename, "condition " + std::to_string(index) + " of furniture type", path, true);
                            if (list) {
                                conditions.push_back([list](GameAPI::GameObject object) { return !list.contains(object); });
                            }
                        } else if (type == "markercount") {
                            std::string markertype;
                            JsonUtil::loadString(jsonCondition, markertype, "markertype", filename, "condition " + std::to_string(index) + " of furniture type", true);
                            std::string condition;
                            JsonUtil::loadString(jsonCondition, condition, "condition", filename, "condition " + std::to_string(index) + " of furniture type", true);
                            int value = -1;
                            JsonUtil::loadInt(jsonCondition, value, "value", filename, "condition " + std::to_string(index) + " of furniture type", true);
                            float minZ = std::numeric_limits<float>::quiet_NaN();
                            JsonUtil::loadFloat(jsonCondition, minZ, "minZ", filename, "condition " + std::to_string(index) + " of furniture type", false);
                            float maxZ = std::numeric_limits<float>::quiet_NaN();
                            JsonUtil::loadFloat(jsonCondition, maxZ, "maxZ", filename, "condition " + std::to_string(index) + " of furniture type", false);

                            if (!markertype.empty() && !condition.empty() && value != -1) {
                                StringUtil::toLower(&markertype);
                                StringUtil::toLower(&condition);
                                GameAPI::GameObjectMarkerType markerEnum = GameAPI::GameObjectMarkerType::NOMARKERTYPE;
                                if (markertype == "sleep") {
                                    markerEnum = GameAPI::GameObjectMarkerType::SLEEP;
                                } else if (markertype == "sit") {
                                    markerEnum = GameAPI::GameObjectMarkerType::SIT;
                                } else {
                                    logger::warn("condition {} o furniture type {} has unknown markertype '{}' defined", index, filename, markertype);
                                }

                                if (markerEnum != GameAPI::GameObjectMarkerType::NOMARKERTYPE) {
                                    if (condition == "equals") {
                                        conditions.push_back([markerEnum, value, minZ, maxZ](GameAPI::GameObject object) {return object.countMarkers(markerEnum, minZ, maxZ) == value;});
                                    } else if (condition == "greater") {
                                        conditions.push_back([markerEnum, value, minZ, maxZ](GameAPI::GameObject object) {return object.countMarkers(markerEnum, minZ, maxZ) > value;});
                                    } else {
                                        logger::warn("condition {} o furniture type {} has unknown condition '{}' defined", index, filename, condition);
                                    }
                                }
                            }
                        } else if (type == "cellname") {
                            std::string cellname;
                            JsonUtil::loadString(jsonCondition, cellname, "cellname", filename, "condition " + std::to_string(index) + " of furniture type", true);
                            if (!cellname.empty()) {
                                StringUtil::toLower(&cellname);
                                conditions.push_back([cellname](GameAPI::GameObject object) {
                                    std::string cellID = object.getParentCellID();
                                    StringUtil::toLower(&cellID);
                                    return cellID.find(cellname) != std::string::npos;
                                });
                            }
                        } else {
                            logger::warn("condition {} of furniture type {} has unknwon type '{}' defined", index, filename, type);
                        }

                        index++;
                    }

                    furnitureType.condition = [conditions](GameAPI::GameObject object) {
                        for (auto& condition : conditions) {
                            if (!condition(object)) {
                                return false;
                            }
                        }
                        return true;
                    };
                } else {
                    logger::warn("porperty 'conditions' of furniture type {} isn't a list", filename);
                }
            }

            furnitureTypes[furnitureType.id] = furnitureType;
        });

        FurnitureType none("none");
        none.name = "None";
        furnitureTypes["none"] = none;

        for (auto& [subtype, supertype] : rawSuperTypes) {
            if (furnitureTypes.contains(supertype)) {
                furnitureTypes[subtype].supertype = &furnitureTypes[supertype];
            } else {
                logger::warn("supertype '{}' of furniture type '{}' doesn't exist", supertype, subtype);
            }
        }
    }

    FurnitureType* FurnitureTable::getFurnitureType(GameAPI::GameObject object, bool inUseCheck) {
        if (!object || object.isDisabled() || object.isDeleted()) {
            return &furnitureTypes["none"];
        }

        if (inUseCheck) {
            if (object.isInUse()) {
                return &furnitureTypes["none"];
            }

            if (Threading::ThreadManager::GetSingleton()->findThread(object)) {
                return &furnitureTypes["none"];
            }
        }

        FurnitureType* currentType = nullptr;
        for (auto& [id, type] : furnitureTypes) {
            if ((!currentType || currentType->priority < type.priority) && type.condition(object)) {
                currentType = &type;
            }
        }

        if (currentType) {
            return currentType;
        } else {
            return &furnitureTypes["none"];
        }
    }

    FurnitureType* FurnitureTable::getFurnitureType(std::string type) {
        StringUtil::toLower(&type);

        auto iter = furnitureTypes.find(type);
        if (iter != furnitureTypes.end()) {
            return &iter->second;
        }

        return &furnitureTypes["none"];
    }
}