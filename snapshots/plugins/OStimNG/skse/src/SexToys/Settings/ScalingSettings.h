#pragma once

#include "ScalingType.h"

#include "Serialization/DeserializationInfo.h"
#include "Serialization/ResetInfo.h"
#include "Serialization/SerializationInfo.h"

namespace Toys {
	namespace Settings {
		struct ScalingSettings {
        public:
            static constexpr ScalingType scalingTypeDefault = ScalingType::INDIVIDUAL_SCALING;
            static constexpr float minScaleDefault = 0.0f;
            static constexpr float maxScaleDefault = 1.0f;
            static constexpr float minMagnitudeDefault = 0.1f;
            static constexpr float maxMagnitudeDefault = 0.5f;
            static constexpr bool doPeaksDefault = true;
            static constexpr float minPeakDefault = 0.6f;
            static constexpr float maxPeakDefault = 1.0f;

            ScalingType scalingType = scalingTypeDefault;
            float minScale = minScaleDefault;
            float maxScale = maxScaleDefault;
            float minMagnitude = minMagnitudeDefault;
            float maxMagnitude = maxMagnitudeDefault;
            bool doPeaks = doPeaksDefault;
            float minPeak = minPeakDefault;
            float maxPeak = maxPeakDefault;

            void serialize(Serialization::SerializationInfo& info);
            void deserialize(Serialization::DeserializationInfo& info);
            void reset(Serialization::ResetInfo& info);

            void writeJson(json& json);
            void readJson(json& json);
		};
	}
}