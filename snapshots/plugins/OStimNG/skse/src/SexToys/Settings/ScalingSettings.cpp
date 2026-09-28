#include "ScalingSettings.h"

#include "Util/JsonUtil.h"

namespace Toys {
    namespace Settings {
        void ScalingSettings::serialize(Serialization::SerializationInfo& info) {
            info.serial.write<ScalingType>(scalingType);
            info.serial.write<float>(minScale);
            info.serial.write<float>(maxScale);
            info.serial.write<float>(minMagnitude);
            info.serial.write<float>(maxMagnitude);
            info.serial.write<bool>(doPeaks);
            info.serial.write<float>(minPeak);
            info.serial.write<float>(maxPeak);
        }

        void ScalingSettings::deserialize(Serialization::DeserializationInfo& info) {
            scalingType = info.serial.read<ScalingType>();
            minScale = info.serial.read<float>();
            maxScale = info.serial.read<float>();
            minMagnitude = info.serial.read<float>();
            maxMagnitude = info.serial.read<float>();
            doPeaks = info.serial.read<bool>();
            minPeak = info.serial.read<float>();
            maxPeak = info.serial.read<float>();
        }

        void ScalingSettings::reset(Serialization::ResetInfo& info) {
            scalingType = scalingTypeDefault;
            minScale = minScaleDefault;
            maxScale = maxScaleDefault;
            minMagnitude = minMagnitudeDefault;
            maxMagnitude = maxMagnitudeDefault;
            doPeaks = doPeaksDefault;
            minPeak = minPeakDefault;
            maxPeak = maxPeakDefault;
        }


        void ScalingSettings::writeJson(json& json) {
            json["scalingType"] = static_cast<int>(scalingType);
            json["minScale"] = minScale;
            json["maxScale"] = maxScale;
            json["minMagnitude"] = minMagnitude;
            json["maxMagnitude"] = maxMagnitude;
            json["doPeaks"] = doPeaks;
            json["minPeak"] = minPeak;
            json["maxPeak"] = maxPeak;
        }

        void ScalingSettings::readJson(json& json) {
            std::string objectName = "export";

            JsonUtil::consumeInt(json, [this](int value) { scalingType = static_cast<ScalingType>(value); }, "scalingType", objectName, "setting", false);
            JsonUtil::loadFloat(json, minScale, "minScale", objectName, "setting", false);
            JsonUtil::loadFloat(json, maxScale, "maxScale", objectName, "setting", false);
            JsonUtil::loadFloat(json, minMagnitude, "minMagnitude", objectName, "setting", false);
            JsonUtil::loadFloat(json, maxMagnitude, "maxMagnitude", objectName, "setting", false);
            JsonUtil::loadBool(json, doPeaks, "doPeaks", objectName, "setting", false);
            JsonUtil::loadFloat(json, minPeak, "minPeak", objectName, "setting", false);
            JsonUtil::loadFloat(json, maxPeak, "maxPeak", objectName, "setting", false);
        }
    }
}