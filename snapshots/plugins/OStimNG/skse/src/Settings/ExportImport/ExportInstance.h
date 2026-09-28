#pragma once

namespace Settings {
    namespace ExportImport {
        class ExportInstance {
        public:
            inline ExportInstance(json& json) : jsonInstance{json} {}

            inline json& getJson() { return jsonInstance; }

            void writeBoolSimple(std::string addon, std::string setting, bool value);
            void writeIntSimple(std::string addon, std::string setting, int value);
            void writeFloatSimple(std::string addon, std::string setting, float value);
            void writeStringSimple(std::string addon, std::string setting, std::string value);

        private:
            json& jsonInstance;

            json& getAddon(std::string addon);
        };
    }
}