#include "ExportInstance.h"

namespace Settings {
    namespace ExportImport {
        void ExportInstance::writeBoolSimple(std::string addon, std::string setting, bool value) {
            getAddon(addon)[setting] = value;
        }

        void ExportInstance::writeIntSimple(std::string addon, std::string setting, int value) {
            getAddon(addon)[setting] = value;
        }

        void ExportInstance::writeFloatSimple(std::string addon, std::string setting, float value) {
            getAddon(addon)[setting] = value;
        }

        void ExportInstance::writeStringSimple(std::string addon, std::string setting, std::string value) {
            getAddon(addon)[setting] = value;
        }


        json& ExportInstance::getAddon(std::string addon) {
            if (!jsonInstance.contains("skseAddons")) {
                jsonInstance["skseAddons"] = json::object();
            }

            json& skseAddons = jsonInstance["skseAddons"];
            if (!skseAddons.contains(addon)) {
                skseAddons[addon] = json::object();
            }

            return skseAddons[addon];
        }
    }
}