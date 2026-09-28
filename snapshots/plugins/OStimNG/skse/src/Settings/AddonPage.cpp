#include "AddonPage.h"

#include "Implementation/GameVariable/GameVariableDropDownSetting.h"
#include "Implementation/GameVariable/GameVariableKeyMapSetting.h"
#include "Implementation/GameVariable/GameVariableSliderSetting.h"
#include "Implementation/GameVariable/GameVariableToggleSetting.h"
#include "Implementation/SimpleSettingGroup.h"

#include "Util/JsonFileLoader.h"
#include "Util/JsonUtil.h"
#include "Util/StringUtil.h"

namespace Settings {
    const char* SETTINGS_FILE_PATH{"Data/SKSE/Plugins/OStim/settings"};

    void AddonPage::loadJsonAddons() {
        for (SettingGroup* group : jsonGroups) {
            delete group;
        }
        jsonGroups.clear();

        jsonVariables.clear();

        Util::JsonFileLoader::LoadFilesInFolder(SETTINGS_FILE_PATH, [&](std::string path, std::string filename, json json) {
            std::string groupName;
            JsonUtil::loadString(json, groupName, "name", filename, "setting file", true);
            if (groupName.empty()) {
                return;    
            }

            if (!json.contains("settings")) {
                logger::warn("setting file {} doesn't have property 'settings' defined", filename);
                return;    
            }

            nlohmann::json& jsonSettings = json["settings"];
            if (!jsonSettings.is_array()) {
                logger::warn("property 'settings' of setting file {} isn't a string", filename);
                return;
            }

            std::vector<Setting*> settings;

            int index = -1;
            for (nlohmann::json jsonSetting : jsonSettings) {
                index++;
                std::string objectType = "setting " + std::to_string(index) + " of setting file";
                if (!jsonSetting.is_object()) {
                    logger::warn("setting {} of setting file {} is malformed", index, filename);
                    continue;
                }

                if (!jsonSetting.contains("variable")) {
                    logger::warn("setting {} of setting file {} does not have field 'setting' defined", index, filename);
                    continue;
                }

                std::string type = "";
                JsonUtil::loadString(jsonSetting, type, "type", filename, objectType, true);
                if (type.empty()) {
                    continue;
                }
                StringUtil::toLower(&type);

                BaseTypes::CommonSettingParams params;

                JsonUtil::loadString(jsonSetting, params.name, "name", filename, objectType, true);
                if (params.name.empty()) {
                    continue;
                }

                GameAPI::GameVariable variable;
                variable.loadJson(path, jsonSetting["variable"]);
                if (!variable) {
                    continue;
                }
                jsonVariables.push_back(variable);
                
                JsonUtil::loadString(jsonSetting, params.tooltip, "tooltip", filename, objectType, false);

                if (type == "toggle") {
                    BaseTypes::ToggleSettingParams toggleParams;
                    JsonUtil::loadBool(jsonSetting, toggleParams.defaultValue, "defaultValue", filename, objectType, false);
                    settings.push_back(new GameVariableSettings::GameVariableToggleSetting(params, toggleParams, variable));
                } else if (type == "slider") {
                    BaseTypes::SliderSettingParams sliderParams;
                    JsonUtil::loadFloat(jsonSetting, sliderParams.defaultValue, "defaultValue", filename, objectType, false);
                    JsonUtil::loadFloat(jsonSetting, sliderParams.valueStep, "valueStep", filename, objectType, true);
                    JsonUtil::loadFloat(jsonSetting, sliderParams.minValue, "minValue", filename, objectType, false);
                    JsonUtil::loadFloat(jsonSetting, sliderParams.maxValue, "maxValue", filename, objectType, false);
                    settings.push_back(new GameVariableSettings::GameVariableSliderSetting(params, sliderParams, variable));
                } else if (type == "dropdown") {
                    BaseTypes::DropDownSettingParams dropDownParams;
                    JsonUtil::loadStringList(jsonSetting, dropDownParams.options, "options", filename, objectType,
                                             true);
                    if (dropDownParams.options.empty()) {
                        continue;
                    }

                    int defaultIndex = 0;
                    JsonUtil::loadInt(jsonSetting, defaultIndex, "defaultIndex", filename, objectType, false);
                    dropDownParams.defaultIndex = defaultIndex;
                    settings.push_back(
                        new GameVariableSettings::GameVariableDropDownSetting(params, dropDownParams, variable));
                } else if (type == "keymap"){
                    BaseTypes::KeyMapSettingParams keyMapParams;
                    int defaultKey = 0;
                    JsonUtil::loadInt(jsonSetting, defaultKey, "defaultKalue", filename, objectType, false);
                    keyMapParams.defaultKey = defaultKey;
                    settings.push_back(new GameVariableSettings::GameVariableKeyMapSetting(params, keyMapParams, variable));
                } else {
                    logger::warn("setting {} of setting file {} has unknown type", index, filename, type);
                }
            }

            jsonGroups.push_back(new SimpleSettingGroup(groupName, SettingDisplayOrder::TOP_TO_BOTTOM, settings));
        });
    }


    void AddonPage::onMenuOpened() {
    }

    std::string AddonPage::getName() {
        return "$ostim_page_addons";
    }

    SettingDisplayOrder AddonPage::getDisplayOrder() {
        return SettingDisplayOrder::TOP_TO_BOTTOM;
    }


    settingGroupIndex AddonPage::getGroupCount() {
        return static_cast<settingGroupIndex>(skseGroups.size() + jsonGroups.size());
    }

    SettingGroup* AddonPage::getGroup(settingGroupIndex index) {
        if (index < 0) {
            return nullptr;
        }

        if (index < skseGroups.size()) {
            return skseGroups[index];
        }

        index -= skseGroups.size();
        if (index < jsonGroups.size()) {
            return jsonGroups[index];
        }

        return nullptr;
    }


    void AddonPage::writeJson(json& json) {
        nlohmann::json addons = json::object();

        for (GameAPI::GameVariable variable : jsonVariables) {
            addons[variable.getIdentifier().toString()] = variable.getValue();
        }

        json["jsonAddons"] = addons;
    }

    void AddonPage::readJson(json& json) {
        if (!json.contains("jsonAddons")) {
            return;
        }

        nlohmann::json& addons = json["jsonAddons"];
        if (!addons.is_object()) {
            return;
        }

        for (GameAPI::GameVariable variable : jsonVariables) {
            std::string id = variable.getIdentifier().toString();
            if (!addons.contains(id)) {
                continue;
            }

            nlohmann::json value = addons[id];
            if (!value.is_number_float()) {
                return;
            }

            variable.setValue(value);
        }
    }
}