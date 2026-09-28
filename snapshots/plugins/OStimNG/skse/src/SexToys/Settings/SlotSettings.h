#pragma once

#include "ClimaxSettings.h"
#include "ScalingSettings.h"
#include "SynchronizationSettings.h"

namespace Toys {
    namespace Settings {
        struct SlotSettings {
        public:
            inline static std::string GLOBAL_KEY = "all";
            static constexpr bool enabledDefault = true;

            bool enabled = enabledDefault;
            SynchronizationSettings synchronizationSettings;
            ScalingSettings scalingSettings;
            ClimaxSettings climaxSettings;

            void serialize(Serialization::SerializationInfo& info);
            void deserialize(Serialization::DeserializationInfo& info);

            json toJson();
            void loadJson(json& json);
        };
    }
}