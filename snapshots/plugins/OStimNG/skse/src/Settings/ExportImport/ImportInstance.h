#pragma once

namespace Settings {
    namespace ExportImport {
        class ImportInstance {
        public:
            inline ImportInstance(json& json) : jsonInstance{json} {}

            inline json& getJson() { return jsonInstance; }

            bool readBoolSimple(std::string addon, std::string setting, bool fallback);
            int readIntSimple(std::string addon, std::string setting, int fallback);
            float readFloatSimple(std::string addon, std::string setting, float fallback);
            std::string* readStringSimple(std::string addon, std::string setting, std::string fallback);

        private:
            json& jsonInstance;
            // the return value of readStringSimple needs to exist outside of the scope of the function
            // otherwise passing a pointer to the underlying c_str will cause an INVALID_ACCESS_EXCEPTION
            std::string exportString = "";

            json* getSetting(std::string addon, std::string setting);
        };
    }
}