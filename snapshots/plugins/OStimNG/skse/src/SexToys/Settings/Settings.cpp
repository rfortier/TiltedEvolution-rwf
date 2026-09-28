#include "Settings.h"

#include "Util/JsonUtil.h"
#include "Util/SerializationUtil.h"

namespace Toys {
    namespace Settings {
        ToySettings* Settings::getToySettings(std::string& id) {
            return &toySettings.insert({id, {}}).first->second;
        }


        void Settings::serialize(Serialization::SerializationInfo& info) {
            info.serial.write<size_t>(toySettings.size());
            for (auto& [toy, setting] : toySettings) {
                SerializationUtil::writeString(info.serial, toy);
                setting.serialize(info);
            }
        }

        void Settings::deserialize(Serialization::DeserializationInfo& info) {
            toySettings.clear();
            for (size_t size = info.serial.read<size_t>(); size > 0; size--) {
                std::string toy = SerializationUtil::readString(info.serial);
                ToySettings settings;
                settings.deserialize(info);
                toySettings.emplace(toy, settings);
            }
        }

        void Settings::reset(Serialization::ResetInfo& info) {
            toySettings.clear();
        }


        void Settings::writeJson(json& json) {
            json["toys"] = json::object();
            nlohmann::json toys = json["toys"];

            toys["toySettings"] = json::object();
            for (auto& [toy, setting] : toySettings) {
                toys["toySettings"][toy] = setting.toJson();
            }
        }

        void Settings::loadJson(json& json) {
            if (!json.contains("toys") || !json["toys"].is_object()) {
                return;
            }

            std::string objectName = "export";

            nlohmann::json& toys = json["toys"];
            
            toySettings.clear();
            if (toys.contains("toySettings") && toys["toySettings"].is_object()) {
                for (auto& [toy, value] : toys["toySettings"].items()) {
                    ToySettings settings;
                    settings.loadJson(value);
                    toySettings.emplace(toy, settings);
                }
            }
        }
    }
}