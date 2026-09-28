#include "MCMTable.h"

#include "Serial/Manager.h"
#include "Settings/SettingTable.h"
#include "Util/CompatibilityTable.h"
#include "Util.h"

namespace MCM {
    void MCMTable::setupForms() {
        for (auto& [formID, setting] : settings) {
            setting.setup(formID);
        }
    }

    void MCMTable::resetValues() {
        undressingMask = 0x3D8BC39D;
    }

    void MCMTable::restoreDefaults() {
        for (auto& [formID, setting] : settings) {
            setting.restoreDefault();
        }
        undressingMask = 0x3D8BC39D;
    }


    bool MCMTable::useRumble() {
        return RE::BSInputDeviceManager::GetSingleton()->IsGamepadEnabled() && settings[0xE11].asBool();
    }


    uint32_t MCMTable::removeWeaponsWithSlot() {
        // the stripping function will treat this as a slotmask
        // so if we ever want to support multiple slots here in the future we just need to change the return value of this function
        return 1 << static_cast<int>(settings[0xDAE].asInt() - 30);
    }


    bool MCMTable::useSoSSex() {
        return Compatibility::CompatibilityTable::sosInstalled() && settings[0xE04].asBool();
    }

    bool MCMTable::useTNGSex() {
        return Compatibility::CompatibilityTable::tngInstalled() && settings[0xECC].asBool();
    }

    bool MCMTable::futaUseMaleExcitement() {
        return Compatibility::CompatibilityTable::customGenitalInstalled() && settings[0xE04].asBool() && settings[0xE05].asBool();
    }

    bool MCMTable::futaUseMaleClimax() {
        return Compatibility::CompatibilityTable::customGenitalInstalled() && settings[0xE04].asBool() && settings[0xE17].asBool();
    }
    
    bool MCMTable::futaUseMaleLight() {
        return Compatibility::CompatibilityTable::customGenitalInstalled() && settings[0xE04].asBool() && settings[0xE25].asBool();
    }

    bool MCMTable::futaUseMaleRole() {
        return Compatibility::CompatibilityTable::customGenitalInstalled() && settings[0xE04].asBool() && settings[0xE48].asBool();
    }


    void MCMTable::exportSettings() {
        json json = json::object();

        for (auto& [formID, setting] : settings) {
            setting.exportSetting(json);
        }

        json["SetUndressingSlotMask"] = undressingMask;

        Serialization::exportSettings(json);
        Settings::SettingTable::getSingleton()->writeJson(json);

        auto ostimPath = util::ostim_path();
        std::filesystem::create_directory(*ostimPath);

        const auto settings_path = util::settings_path();
        std::ofstream file(*settings_path);
        file << std::setw(2) << json << std::endl;
    }

    void MCMTable::importSettings() {
        auto settings_path = util::settings_path();
        if (!fs::exists(*settings_path)) {
            settings_path = util::settings_path_legacy();
            if (!fs::exists(*settings_path)) {
                logger::warn("settings file doesn't exist or no access");
                return;
            }
        }

        std::ifstream ifs(*settings_path);
        json json = json::parse(ifs, nullptr, false);

        if (json.is_discarded()) {
            logger::warn("settings file is malformed");
            return;
        }

        for (auto& [formID, setting] : settings) {
            setting.importSetting(json);
        }

        if (json.contains("SetUndressingSlotMask")) {
            undressingMask = json["SetUndressingSlotMask"];
        } else {
            undressingMask = 0x3D8BC39D;
        }

        Serialization::importSettings(json);
        Settings::SettingTable::getSingleton()->readJson(json);
    }
}