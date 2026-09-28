#include "SlotSettings.h"

#include "Util/JsonUtil.h"

namespace Toys {
    namespace Settings {
        void SlotSettings::serialize(Serialization::SerializationInfo& info) {
            info.serial.write<bool>(enabled);
            synchronizationSettings.serialize(info);
            scalingSettings.serialize(info);
            climaxSettings.serialize(info);
        }

        void SlotSettings::deserialize(Serialization::DeserializationInfo& info) {
            enabled = info.serial.read<bool>();
            synchronizationSettings.deserialize(info);
            scalingSettings.deserialize(info);
            climaxSettings.deserialize(info);
        }

        json SlotSettings::toJson() {
            json json = json::object();

            json["enabled"] = enabled;
            synchronizationSettings.writeJson(json);
            scalingSettings.writeJson(json);
            climaxSettings.writeJson(json);

            return json;
        }

        void SlotSettings::loadJson(json& json) {
            if (!json.is_object()) {
                return;
            }

            std::string objectName = "export";

            JsonUtil::loadBool(json, enabled, "enabled", objectName, "setting", false);
            synchronizationSettings.writeJson(json);
            scalingSettings.readJson(json);
            climaxSettings.readJson(json);
        }
    }
}