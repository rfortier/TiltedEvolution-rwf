#include "TraitTable.h"

#include "FacialExpression.h"

#include "Graph/GraphTable.h"
#include "Serial/Manager.h"
#include "Util/ActorUtil.h"
#include "Util/MapUtil.h"
#include "Util/VectorUtil.h"
#include "Util/JsonFileLoader.h"
#include "Util/JsonUtil.h"
#include "Util/LookupTable.h"
#include "Util/StringUtil.h"

namespace Trait {
    const char* EXPRESSION_FILE_PATH{"Data/SKSE/Plugins/OStim/facial expressions"};
    const char* EQUIP_OBJECT_FILE_PATH{"Data/SKSE/Plugins/OStim/equip objects"};

    void TraitTable::setup() {
        openMouthPhonemes.insert({0, {.type = 0, .baseValue = 100}});
        openMouthPhonemes.insert({1, {.type = 1, .baseValue = 40}});
        openMouthPhonemes.insert({5, {.type = 5, .baseValue = 100}});

        lickingPhonemes.insert({0, {.type = 0, .baseValue = 100}});

        std::srand((unsigned)time(NULL));

        Util::JsonFileLoader::LoadFilesInFolder(EXPRESSION_FILE_PATH, [&](std::string path,std::string filename,json json) {
            FacialExpression* expression = new FacialExpression();
            if (json.contains("female")) {
                parseGender(path, json["female"], &expression->female);
            }

            if (json.contains("male")) {
                parseGender(path, json["male"], &expression->male);
            }

            if (json.contains("sets")) {
                for (auto& set : json["sets"]) {
                    addToTable(&expressionsBySets, set, expression);
                }
            }

            if (json.contains("events")) {
                for (auto& eventStr : json["events"]) {
                    addToTable(&expressionsByEvents, eventStr, expression);
                }
            }

            if (json.contains("actionActors")) {
                for (auto& action : json["actionActors"]) {
                    addToTable(&expressionsByActionActors, Graph::GraphTable::getActionAlias(action), expression);
                }
            }

            if (json.contains("actionTargets")) {
                for (auto& action : json["actionTargets"]) {
                    addToTable(&expressionsByActionTargets, Graph::GraphTable::getActionAlias(action), expression);
                }
            }
        });

        auto iter = expressionsBySets.find("default");
        if (iter == expressionsBySets.end()) {
            logger::warn("no default expression defined, generating empty");
            std::vector<FacialExpression*>* expressions = new std::vector<FacialExpression*>();
            expressions->push_back(new FacialExpression());
            expressionsBySets.insert({"default", expressions});
        }
    }

    void TraitTable::setupForms() {
        RE::TESDataHandler* handler = RE::TESDataHandler::GetSingleton();
        noFacialExpressionsFaction = handler->LookupForm<RE::TESFaction>(0xD92, "OStim.esp");

        // equip objects
        Util::JsonFileLoader::LoadFilesInFolder(EQUIP_OBJECT_FILE_PATH, [&](std::string path, std::string filename, json json) {
            std::string id = filename;
            StringUtil::toLower(&id);

            if (id == "default" || id == "random") {
                logger::info("illegal equip object id: {}, this id is reserved by OStim", id);
                return;
            }

            if (!json.contains("type")) {
                logger::info("file {} does not have field 'type' defined", path);
                return;
            }
            if (!json.contains("name")) {
                logger::info("file {} does not have field 'name' defined", path);
                return;
            }

            EquipObject* object = new EquipObject();
            object->item.loadJson(path, json);
            if (!object->item) {
                return;
            }

            JsonUtil::loadTranslatedString(json, object->name, "name", id, "equip object", true);

            if (json.contains("variants")) {
                for (auto& [key, val] : json["variants"].items()) {
                    GameAPI::GameArmor variant;
                    variant.loadJson(path, val);
                    if (variant) {
                        object->variants.emplace(key, variant);
                    }
                }
            }

            std::string type = json["type"];
            StringUtil::toLower(&type);
            auto iter = equipObjects.find(type);
            if (iter != equipObjects.end()) {
                if (iter->second.contains(id)) {
                    logger::info("duplicate equip object {} for type {}", id, type);
                } else {
                    iter->second.emplace(id, object);
                }
            } else {
                equipObjects.emplace(type, std::unordered_map<std::string, EquipObject*>{{id, object}});
            }

            bool isDefault = false;
            JsonUtil::loadBool(json, isDefault, "default", filename, "equip object", false);
            if (isDefault) {
                defaultEquipObjects[type] = object;
            }
        });
    }

    void TraitTable::parseGender(std::string& path, nlohmann::json json, GenderExpression* genderExpression) {
        if (json.contains("duration")) {
            genderExpression->duration = json["duration"];
        }

        if (json.contains("expression")) {
            genderExpression->expression = parseModifier(path, json["expression"]);
            genderExpression->typeMask |= ExpressionType::EXPRESSION;
        }

        if (json.contains("modifiers")) {
            for (auto& modifier : json["modifiers"]) {
                auto mod = parseModifier(path, modifier);
                if (VectorUtil::contains(eyelidModifierTypes, mod.type)) {
                    genderExpression->eyelidModifiers.insert({mod.type, mod});
                    genderExpression->typeMask |= ExpressionType::LID_MODIFIER;
                } else if (VectorUtil::contains(eyebrowModifierTypes, mod.type)) {
                    genderExpression->eyebrowModifiers.insert({mod.type, mod});
                    genderExpression->typeMask |= ExpressionType::BROW_MODIFIER;
                } else if (VectorUtil::contains(eyeballModifierTypes, mod.type)) {
                    genderExpression->eyeballModifiers.insert({mod.type, mod});
                    genderExpression->typeMask |= ExpressionType::BALL_MODIFIER;
                }
            }
        }

        if (json.contains("phonemes")) {
            for (auto& phoneme : json["phonemes"]) {
                auto mod = parseModifier(path, phoneme);
                genderExpression->phonemes.insert({mod.type, mod});
            }
            genderExpression->typeMask |= ExpressionType::PHONEME;
        }

        if (json.contains("phonemeObjectThreshold")) {
            genderExpression->phonemeObjectThreshold = json["phonemeObjectThreshold"];
        }

        if (json.contains("phonemeObjects")) {
            for (auto& object : json["phonemeObjects"]) {
                genderExpression->phonemeObjects.push_back(object);
            }
        }
    }

    FaceModifier TraitTable::parseModifier(std::string& path, nlohmann::json json) {
        FaceModifier modifier{};
        if (json.contains("type")) {
            modifier.type = json["type"];
        }
        if (json.contains("baseValue")) {
            modifier.baseValue = json["baseValue"];
        }
        if (json.contains("variance")) {
            modifier.variance = json["variance"];
        }
        if (json.contains("speedMultiplier")) {
            modifier.speedMultiplier = json["speedMultiplier"];
        }
        if (json.contains("excitementMultiplier")) {
            modifier.excitementMultiplier = json["excitementMultiplier"];
        }
        if (json.contains("faction")) {
            modifier.faction.loadJson(path, json["faction"]);
        }
        if (json.contains("factionFallback")) {
            modifier.factionFallback = json["factionFallback"];
        }
        if (json.contains("delay")) {
            modifier.delay = json["delay"];
        }
        if (json.contains("delayVariance")) {
            modifier.delayVariance = json["delayVariance"];
        }
        return modifier;
    }

    void TraitTable::addToTable(std::unordered_map<std::string, std::vector<FacialExpression*>*>* table, std::string key, FacialExpression* expression) {
        StringUtil::toLower(&key);
        auto iter = table->find(key);
        if (iter != table->end()) {
            iter->second->push_back(expression);
        } else {
            std::vector<FacialExpression*>* expressions = new std::vector<FacialExpression*>();
            expressions->push_back(expression);
            table->insert({key, expressions});
        }
    }


    std::vector<FacialExpression*>* TraitTable::getExpressionsForActionActor(std::string action) {
        return getExpressionsFromTable(expressionsByActionActors, action);
    }

    std::vector<FacialExpression*>* TraitTable::getExpressionsForActionTarget(std::string action) {
        return getExpressionsFromTable(expressionsByActionTargets, action);
    }

    std::vector<FacialExpression*>* TraitTable::getExpressionsForEvent(std::string strEvent) {
        return getExpressionsFromTable(expressionsByEvents, strEvent);
    }

    std::vector<FacialExpression*>* TraitTable::getExpressionsForSet(std::string set) {
        return getExpressionsFromTable(expressionsBySets, set);
    }

    std::vector<FacialExpression*>* TraitTable::getExpressionsFromTable(std::unordered_map<std::string, std::vector<FacialExpression*>*> table, std::string key) {
        auto iter = table.find(key);
        if (iter != table.end()) {
            return iter->second;
        }
        return nullptr;
    }


    EquipObject* TraitTable::getRandomEquipObject(std::string type) {
        auto iter = equipObjects.find(type);
        if (iter != equipObjects.end()) {
            return MapUtil::randomValue(iter->second);
        }
        return nullptr;
    }

    EquipObject* TraitTable::getEquipObject(GameAPI::GameActor actor, std::string type) {
        StringUtil::toLower(&type);
        auto iter = equipObjects.find(type);
        if (iter == equipObjects.end()) {
            return nullptr;
        }

        std::string id = Serialization::getEquipObject(actor.getBaseFormID().formID, type);
        if (id == "random") {
            return MapUtil::randomValue(iter->second);
        } else if (id != "" && id != "default") {
            auto iter2 = iter->second.find(id);
            if (iter2 != iter->second.end()) {
                return iter2->second;
            }
        }

        id = Serialization::getEquipObject(actor.getRace().getFormID(), type);
        if (id == "random") {
            return MapUtil::randomValue(iter->second);
        } else if (id != "" && id != "default") {
            auto iter2 = iter->second.find(id);
            if (iter2 != iter->second.end()) {
                return iter2->second;
            }
        }

        id = Serialization::getEquipObject(actor.isSex(GameAPI::GameSex::FEMALE) ? 0x1 : 0x0, type);
        if (id == "random") {
            return MapUtil::randomValue(iter->second);
        } else if (id != "" && id != "default") {
            auto iter2 = iter->second.find(id);
            if (iter2 != iter->second.end()) {
                return iter2->second;
            }
        }

        auto iter2 = defaultEquipObjects.find(type);
        if (iter2 != defaultEquipObjects.end()) {
            return iter2->second;
        }

        return MapUtil::randomValue(iter->second);
    }

    std::vector<std::string> TraitTable::getEquipObjectsOfType(std::string type) {
        std::vector<std::string> ret;
        if (auto iter = equipObjects.find(type); iter != equipObjects.end()) {
            for (auto& object : iter->second) {
                ret.push_back(object.second->name);
           }
        }
        return ret;
    }

    std::vector<std::string> TraitTable::getEquipObjectTypes() {
        std::vector<std::string> types;

        for (auto& [type, objects] : equipObjects) {
            types.push_back(type);
        }

        return types;
    }

    std::vector<std::string> TraitTable::getEquipObjectPairs(RE::FormID formID, std::string type) {
        std::vector<std::string> ret;
        if (defaultEquipObjects.contains(type) || formID > 1) {
            ret.push_back("default");
            ret.push_back("$ostim_generic_default");
        }
        ret.push_back("random");
        ret.push_back("$ostim_generic_random");

        std::vector<std::pair<std::string, EquipObject*>> objects;

        auto iter = equipObjects.find(type);
        if (iter != equipObjects.end()) {
            for (auto& [id, object] : iter->second) {
                objects.push_back({id, object});
            }
        }

        std::sort(objects.begin(), objects.end(), [&](std::pair<std::string, EquipObject*> pairA, std::pair<std::string, EquipObject*> pairB) {
            return pairA.first.compare(pairB.first) <= 0;
        });

        for (auto& [id, object] : objects) {
            ret.push_back(id);
            ret.push_back(object->name);
        }

        return ret;
    }

    std::string TraitTable::getEquipObjectName(RE::FormID formID, std::string type) {
        std::string id = Serialization::getEquipObject(formID, type);
        if (id == "") {
            return defaultEquipObjects.contains(type) || formID > 1 ? "$ostim_generic_default" : "$ostim_generic_random";
        } else if (id == "default") {
            return "$ostim_generic_default";
        } else if (id == "random") {
            return "$ostim_generic_random";
        }

        auto iter = equipObjects.find(type);
        if (iter == equipObjects.end()) {
            return "$ostim_generic_invalid";
        }

        auto iter2 = iter->second.find(id);
        if (iter2 != iter->second.end()) {
            return iter2->second->name;
        }

        return "$ostim_generic_invalid";
    }

    void TraitTable::setEquipObjectID(RE::FormID formID, std::string type, std::string id) {
        StringUtil::toLower(&type);
        StringUtil::toLower(&id);
        Serialization::setEquipObject(formID, type, id);
    }
}