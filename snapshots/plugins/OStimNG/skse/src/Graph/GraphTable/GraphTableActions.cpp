#include "Graph/GraphTable.h"

#include "SexToys/ToyTable.h"
#include "Util/JsonFileLoader.h"
#include "Util/JsonUtil.h"
#include "Util/MapUtil.h"
#include "Util/StringUtil.h"

namespace Graph {
    const char* ACTION_FILE_PATH{"Data/SKSE/Plugins/OStim/actions"};
    const char* ACTION_TAG_FILE_PATH{"Data/SKSE/Plugins/OStim/action tags"};

    Action::ActionActor parseActionActor(std::string path, std::string filename, json& json) {
        Action::ActionActor actor;

        JsonUtil::loadFloat(json, actor.stimulation, "stimulation", filename, "action", false);
        JsonUtil::loadFloat(json, actor.maxStimulation, "maxStimulation", filename, "action", false);
        JsonUtil::loadBool(json, actor.fullStrip, "fullStrip", filename, "action", false);
        JsonUtil::loadBool(json, actor.moan, "moan", filename, "action", false);
        JsonUtil::loadBool(json, actor.talk, "talk", filename, "action", false);
        JsonUtil::loadBool(json, actor.muffled, "muffled", filename, "action", false);
        JsonUtil::loadLowerString(json, actor.expressionOverride, "expressionOverride", filename, "action", false);

        JsonUtil::consumeLowerStringList(json, [&actor](std::string requirement) { actor.requirements.insert(requirement); }, "requirements", filename, "action", false);

        if (json.contains("strippingSlots")) {
            for (auto& slot : json["strippingSlots"]) {
                actor.strippingMask |= 1 << (slot.get<int>() - 30);
            }
        }

        JsonUtil::consumeLowerStringList(json, [&actor](std::string equipObject) { actor.equipObjects.insert(equipObject); }, "equipObject", filename, "action", false);

        JsonUtil::loadGameRecordList(json, path, "faction", actor.factions);
        JsonUtil::loadGameRecordList(json, path, "statFaction", actor.statFactions);
        JsonUtil::loadGameRecordList(json, path, "playerStatFaction", actor.playerStatFactions);
        JsonUtil::loadGameRecordList(json, path, "climaxStatFaction", actor.climaxStatFactions);
        JsonUtil::loadGameRecordList(json, path, "partnerClimaxStatFaction", actor.partnerClimaxStatFactions);
        JsonUtil::loadGameRecordList(json, path, "playerClimaxStatFaction", actor.playerClimaxStatFactions);
        JsonUtil::loadGameRecordList(json, path, "playerPartnerClimaxStatFaction", actor.playerPartnerClimaxStatFactions);
        JsonUtil::loadGameRecordList(json, path, "playerStatList", actor.playerStatLists);
        JsonUtil::loadGameRecordList(json, path, "playerClimaxStatList", actor.playerClimaxStatLists);
        JsonUtil::loadGameRecordList(json, path, "playerPartnerClimaxStatList", actor.playerPartnerClimaxStatLists);

        actor.displayStatFaction = actor.statFactions.empty() ? nullptr : actor.statFactions[0];
        actor.displayPlayerStatFaction = actor.playerStatFactions.empty() ? nullptr : actor.playerStatFactions[0];
        actor.displayClimaxStatFaction = actor.climaxStatFactions.empty() ? nullptr : actor.climaxStatFactions[0];
        actor.displayPartnerClimaxStatFaction = actor.partnerClimaxStatFactions.empty() ? nullptr : actor.partnerClimaxStatFactions[0];
        actor.displayPlayerClimaxStatFaction = actor.playerClimaxStatFactions.empty() ? nullptr : actor.playerClimaxStatFactions[0];
        actor.displayPlayerPartnerClimaxStatFaction = actor.playerPartnerClimaxStatFactions.empty() ? nullptr : actor.playerPartnerClimaxStatFactions[0];

        actor.displayPlayerStatList = actor.playerStatLists.empty() ? nullptr : actor.playerStatLists[0];
        actor.displayPlayerClimaxStatList = actor.playerClimaxStatLists.empty() ? nullptr : actor.playerClimaxStatLists[0];
        actor.displayPlayerPartnerClimaxStatList = actor.playerPartnerClimaxStatLists.empty() ? nullptr : actor.playerPartnerClimaxStatLists[0];

        if (json.contains("ints")) {
            for (auto& [key, val] : json["ints"].items()) {
                std::string mutableKey = key;
                StringUtil::toLower(&mutableKey);
                actor.ints.insert(std::make_pair(mutableKey, val.get<int>()));
            }
        }

        if (json.contains("intLists")) {
            for (auto& [key, val] : json["intLists"].items()) {
                std::string mutableKey = key;
                StringUtil::toLower(&mutableKey);
                std::vector<int> ints;
                for (auto& entry : val) {
                    ints.push_back(entry.get<int>());
                }
                actor.intLists.insert(std::make_pair(mutableKey, ints));
            }
        }

        if (json.contains("floats")) {
            for (auto& [key, val] : json["floats"].items()) {
                std::string mutableKey = key;
                StringUtil::toLower(&mutableKey);
                actor.floats.insert(std::make_pair(mutableKey, val.get<float>()));
            }
        }

        if (json.contains("floatLists")) {
            for (auto& [key, val] : json["floatLists"].items()) {
                std::string mutableKey = key;
                StringUtil::toLower(&mutableKey);
                std::vector<float> floats;
                for (auto& entry : val) {
                    floats.push_back(entry.get<float>());
                }
                actor.floatLists.insert(std::make_pair(mutableKey, floats));
            }
        }

        if (json.contains("strings")) {
            auto& strings = json["strings"];
            for (auto& [key, val] : json["strings"].items()) {
                std::string mutableKey = key;
                StringUtil::toLower(&mutableKey);
                std::string value = val.get<std::string>();
                StringUtil::toLower(&value);
                actor.strings.insert(std::make_pair(mutableKey, value));
            }
        }

        if (json.contains("stringLists")) {
            for (auto& [key, val] : json["stringLists"].items()) {
                std::string mutableKey = key;
                StringUtil::toLower(&mutableKey);
                std::vector<std::string> strings;
                for (auto& entry : val) {
                    std::string value = entry.get<std::string>();
                    StringUtil::toLower(&value);
                    strings.push_back(value);
                }
                actor.stringLists.insert(std::make_pair(mutableKey, strings));
            }
        }

        JsonUtil::loadLowerStringList(json, actor.toySlots, "toySlot", filename, "toy slot", false);
        Toys::ToyTable::getSingleton()->addToySlots(actor.toySlots);

        return actor;
    };

    void GraphTable::SetupActions() {
        Util::JsonFileLoader::LoadFilesInFolder(
            ACTION_TAG_FILE_PATH, [&](std::string path, std::string filename, json json) {
                Graph::Action::ActionTag tag;
                tag.tag = filename;
                StringUtil::toLower(&tag.tag);

                tag.roles.forEach([&path, &filename, &json](Role role, Action::ActionActor& actor) {
                    std::string key = *RoleMapAPI::KEYS.get(role);
                    if (json.contains(key)) {
                        actor = parseActionActor(path, filename, json[key]);
                    }
                });

                Graph::Action::ActionTag inverted = tag;
                inverted.tag = "-" + tag.tag;
                inverted.roles.actor = tag.roles.target;
                inverted.roles.target = tag.roles.actor;

                actionTags[tag.tag] = tag;
                actionTags[inverted.tag] = inverted;
            }
        );

        Util::JsonFileLoader::LoadFilesInFolder(
            ACTION_FILE_PATH, [&](std::string path, std::string filename, json json) {
                std::string type = filename;
                StringUtil::toLower(&type);
                if (json.contains("aliases")) {
                    if (json["aliases"].is_array()) {
                        int index = 0;
                        for (auto& alias : json["aliases"]) {
                            if (alias.is_string()) {
                                std::string key = alias;
                                StringUtil::toLower(&key);
                                actionAliases[key] = type;
                            } else {
                                logger::warn("alias {} of action '{}' is not a string", index, filename);
                            }
                            index++;
                        }
                    } else {
                        logger::warn("property 'aliases' of action '{}' is not a list", filename);
                    }
                }

                Graph::Action::ActionAttributes attr;
                attr.type = type;

                attr.roles.forEach([&path, &filename, &json](Role role, Action::ActionActor& actor) {
                    std::string key = *RoleMapAPI::KEYS.get(role);
                    if (json.contains(key)) {
                        actor = parseActionActor(path, filename, json[key]);
                    }
                });

                if (json.contains("peak")) {
                    attr.peakType = Action::Peak::PeakType::fromJson(filename, json["peak"]);
                }

                if (json.contains("sounds")) {
                    for (auto& sound : json["sounds"]) {
                        Sound::SoundType* type = Sound::SoundType::fromJson(path, sound);
                        if (type) {
                            attr.sounds.push_back(type);
                        }
                    }
                }

                if (json.contains("tags")) {
                    for (auto& tag : json["tags"]) {
                        std::string tagStr = tag.get<std::string>();
                        StringUtil::toLower(&tagStr);

                        auto iter = actionTags.find(tagStr);
                        if (iter != actionTags.end()) {
                            attr.tags.push_back(iter->second);
                        } else {
                            logger::warn("Action tag '{}' of action '{}' not found, generating empty.", tagStr, type);
                            Graph::Action::ActionTag tagObj;
                            tagObj.tag = tagStr;
                            attr.tags.push_back(tagObj);
                        }
                    }

                    attr.mergeTags();
                }


                actions[attr.type] = attr;
            });
    }

    std::vector<std::string> GraphTable::getActions() {
        return MapUtil::keyList(actions);
    }

    std::string GraphTable::getActionAlias(std::string type) {
        StringUtil::toLower(&type);
        if (auto it = actionAliases.find(type); it != actionAliases.end()) {
            return it->second;
        }

        return type;
    }

    Action::ActionAttributes* GraphTable::getActionAttributesByType(std::string type) {
        if (auto it = actions.find(type); it != actions.end()) {
            return &it->second;
        } else {
            logger::warn("No action found for {} using default", type);
            return &actions.at("default");
        }
    }


    Action::ActionTag* GraphTable::getActionTag(std::string id) {
        if (auto it = actionTags.find(id); it != actionTags.end()) {
            return &it->second;
        }

        return nullptr;
    }

    Action::ActionAttributes* GraphTable::getActionTypeUnsafe(std::string id) {
        if (auto it = actions.find(id); it != actions.end()) {
            return &it->second;
        }

        return nullptr;
    }
}