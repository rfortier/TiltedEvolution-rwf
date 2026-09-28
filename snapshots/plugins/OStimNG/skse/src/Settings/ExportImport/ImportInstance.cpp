#include "ImportInstance.h"

namespace Settings {
    namespace ExportImport {
        bool ImportInstance::readBoolSimple(std::string addon, std::string setting, bool fallback) {
            json* jsonSetting = getSetting(addon, setting);
            if (!jsonSetting || !jsonSetting->is_boolean()) {
                return fallback;
            }

            return *jsonSetting;
        }

        int ImportInstance::readIntSimple(std::string addon, std::string setting, int fallback) {
            json* jsonSetting = getSetting(addon, setting);
            if (!jsonSetting || !jsonSetting->is_number_integer()) {
                return fallback;
            }

            return *jsonSetting;
        }

        float ImportInstance::readFloatSimple(std::string addon, std::string setting, float fallback) {
            json* jsonSetting = getSetting(addon, setting);
            if (!jsonSetting || !jsonSetting->is_number()) {
                return fallback;
            }

            return *jsonSetting;
        }

        std::string* ImportInstance::readStringSimple(std::string addon, std::string setting, std::string fallback) {
            json* jsonSetting = getSetting(addon, setting);
            if (!jsonSetting || !jsonSetting->is_string()) {
                exportString = fallback;
            } else {
                exportString = *jsonSetting;
            }

            return &exportString;
        }


        json* ImportInstance::getSetting(std::string addon, std::string setting) {
            if (!jsonInstance.contains("skseAddons")) {
                return nullptr;
            }

            json& skseAddons = jsonInstance["skseAddons"];
            if (!skseAddons.is_object() || !skseAddons.contains(addon)) {
                return nullptr;
            }

            json& jsonAddon = skseAddons[addon];
            if (!jsonAddon.is_object() || !jsonAddon.contains(setting)) {
                return nullptr;
            }

            return &jsonAddon[setting];
        }
    }
}