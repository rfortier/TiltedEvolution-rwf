#pragma once

#include "Serialization/DeserializationInfo.h"
#include "Serialization/ResetInfo.h"
#include "Serialization/SerializationInfo.h"

namespace Toys {
    namespace Settings {
        struct ClimaxSettings {
        public:
            static constexpr bool doClimaxDefault = true;
            static constexpr float climaxMagnitudeDefault = 1.0f;

            bool doClimax = doClimaxDefault;
            float climaxMagnitude = climaxMagnitudeDefault;

            void serialize(Serialization::SerializationInfo& info);
            void deserialize(Serialization::DeserializationInfo& info);

            void writeJson(json& json);
            void readJson(json& json);
            void reset(Serialization::ResetInfo& info);
        };
    }
}