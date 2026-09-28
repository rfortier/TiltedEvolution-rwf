#include "ToySettings.h"

#include "Util/JsonUtil.h"
#include "Util/SerializationUtil.h"

namespace Toys {
    namespace Settings {
        SlotSettings* ToySettings::getSlotSettings(std::string slot) {
            return &slotSettings.insert({slot, {}}).first->second;
        }

        SlotSettings* ToySettings::getSlotSettingsUnsafe(std::string slot) {
            auto iter = slotSettings.find(slot);
            return iter == slotSettings.end() ? nullptr : &iter->second;
        }


        void ToySettings::serialize(Serialization::SerializationInfo& info) {
            info.serial.write<size_t>(slotSettings.size());
            for (auto& [slot, settings] : slotSettings) {
                SerializationUtil::writeString(info.serial, slot);
                settings.serialize(info);
            }
        }

        void ToySettings::deserialize(Serialization::DeserializationInfo& info) {
            slotSettings.clear();
            for (size_t size = info.serial.read<size_t>(); size > 0; size--) {
                std::string slot = SerializationUtil::readString(info.serial);
                SlotSettings settings;
                settings.deserialize(info);
                slotSettings.emplace(slot, settings);
            }
        }

        json ToySettings::toJson() {
            json json = json::object();

            json["slotSettings"] = json::object();
            for (auto& [slot, setting] : slotSettings) {
                json["slotSettings"][slot] = setting.toJson();
            }

            return json;
        }

        void ToySettings::loadJson(json& json) {
            if (!json.is_object()) {
                return;
            }

            std::string objectName = "export";

            slotSettings.clear();
            if (json.contains("slotSettings") && json["slotSettings"].is_object()) {
                for (auto& [slot, value] : json["slotSettings"].items()) {
                    SlotSettings settings;
                    settings.loadJson(value);
                    slotSettings.emplace(slot, settings);
                }
            }
        }
    }
}