#pragma once

#include "GameAPI/GameFaction.h"

namespace JsonUtil {
    void loadString(json& json, std::string& value, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadLowerString(json& json, std::string& value, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadTranslatedString(json& json, std::string& value, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadFloat(json& json, float& value, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadInt(json& json, int& value, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadBool(json& json, bool& value, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);

    void loadStringList(json& json, std::vector<std::string>& list, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadLowerStringList(json& json, std::vector<std::string>& list, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadTranslatedStringList(json& json, std::vector<std::string>& list, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadFloatList(json& json, std::vector<float>& list, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadIntList(json& json, std::vector<int>& list, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void loadBoolList(json& json, std::vector<bool>& list, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);

    void consumeString(json& json, std::function<void(std::string)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeLowerString(json& json, std::function<void(std::string)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeTranslatedString(json& json, std::function<void(std::string)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeFloat(json& json, std::function<void(float)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeInt(json& json, std::function<void(int)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeBool(json& json, std::function<void(bool)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);

    void consumeStringList(json& json, std::function<void(std::string)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeLowerStringList(json& json, std::function<void(std::string)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeTranslatedStringList(json& json, std::function<void(std::string)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeFloatList(json& json, std::function<void(float)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeIntList(json& json, std::function<void(int)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeBoolList(json& json, std::function<void(bool)> consumer, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);

    void consumeStringMap(json& json, std::function<void(std::string, std::string)> consumer, bool lowerKeys, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeLowerStringMap(json& json, std::function<void(std::string, std::string)> consumer, bool lowerKeys, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeTranslatedStringMap(json& json, std::function<void(std::string, std::string)> consumer, bool lowerKeys, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeFloatMap(json& json, std::function<void(std::string, float)> consumer, bool lowerKeys, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeIntMap(json& json, std::function<void(std::string, int)> consumer, bool lowerKeys, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);
    void consumeBoolMap(json& json, std::function<void(std::string, bool)> consumer, bool lowerKeys, std::string propertyName, std::string& objectName, std::string objectType, bool warnIfNotExists);

	template <class T>
	void loadGameRecord(json& json, T& value, std::string propertyName, std::string& objectName, std::string objectType, std::string& filepath, bool warnIfNotExists) {
        if (json.contains(propertyName)) {
            if (json[propertyName].is_object()) {
                value.loadJson(filepath, json[propertyName]);
            } else {
                logger::warn("property '{}' of {} {} is malformed", propertyName, objectType, objectName);
            }
        } else if (warnIfNotExists) {
            logger::warn("{} doesn't have property '{}' defined", objectName, propertyName);
        }
	}

    template <class T>
    void loadGameRecordList(json& json, std::string path, std::string key, std::vector<T>& records) {
        if (json.contains(key)) {
            if (json[key].is_array()) {
                for (auto& jsonFaction : json[key]) {
                    T record;
                    record.loadJson(path, jsonFaction);
                    if (record) {
                        records.push_back(record);
                    }
                }
            } else {
                T record;
                record.loadJson(path, json[key]);
                if (record) {
                    records.push_back(record);
                }
            }
        }
    }

	template <class T>
	T* getForm(std::string path, json json, RE::TESDataHandler* handler) {
        if (!json.contains("mod")) {
            logger::info("file {} does not have field 'mod' defined", path);
            return nullptr;
		}

		if (!json.contains("formid")) {
            logger::info("file {} does not have field 'formid' defined", path);
            return nullptr;
		}

		std::string mod = json["mod"];
        std::string formid = json["formid"];
		T* form = handler->LookupForm<T>(std::stoi(formid, nullptr, 16), mod);
        if (!form) {
            logger::info(" file {} contains unkown form {}|{}", path, mod, formid);
		}

		return form;
	}

	template <class T>
	T* getForm(std::string path, json json) {
        return getForm<T>(path, json, RE::TESDataHandler::GetSingleton());
	}
}