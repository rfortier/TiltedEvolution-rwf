#include "ClimaxSettings.h"

#include "Util/JsonUtil.h"

namespace Toys {
    namespace Settings {
        void ClimaxSettings::serialize(Serialization::SerializationInfo& info) {
            info.serial.write<bool>(doClimax);
            info.serial.write<float>(climaxMagnitude);
        }

        void ClimaxSettings::deserialize(Serialization::DeserializationInfo& info) {
            doClimax = info.serial.read<bool>();
            climaxMagnitude = info.serial.read<float>();
        }

        void ClimaxSettings::reset(Serialization::ResetInfo& info) {
            doClimax = doClimaxDefault;
            climaxMagnitude = climaxMagnitudeDefault;
        }


        void ClimaxSettings::writeJson(json& json) {
            json["doClimax"] = doClimax;
            json["climaxMagnitude"] = climaxMagnitude;
        }

        void ClimaxSettings::readJson(json& json) {
            std::string objectName = "export";

            JsonUtil::loadBool(json, doClimax, "doClimax", objectName, "setting", false);
            JsonUtil::loadFloat(json, climaxMagnitude, "climaxMagnitude", objectName, "setting", false);
        }
    }
}