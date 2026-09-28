#pragma once

#include "ToySettings.h"

#include "Util/Singleton.h"

namespace Toys {
    namespace Settings {
        class Settings : public Util::Singleton<Settings> {
        public:
            ToySettings* getToySettings(std::string& id);

            void serialize(Serialization::SerializationInfo& info);
            void deserialize(Serialization::DeserializationInfo& info);
            void reset(Serialization::ResetInfo& info);
            void writeJson(json& json);
            void loadJson(json& json);

        private:
            std::unordered_map<std::string, ToySettings> toySettings;
        };
    }
}