#pragma once

#include "SlotSettings.h"

namespace Toys {
    namespace Settings {
        struct ToySettings {
        public:
            inline static std::string GLOBAL_KEY = "all";

            SlotSettings* getSlotSettings(std::string slot);
            SlotSettings* getSlotSettingsUnsafe(std::string slot);

            void serialize(Serialization::SerializationInfo& info);
            void deserialize(Serialization::DeserializationInfo& info);

            json toJson();
            void loadJson(json& json);

        private:
            std::unordered_map<std::string, SlotSettings> slotSettings;
        };
    }
}