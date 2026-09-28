#include "MCMSetting.h"

namespace MCM {
    MCMSetting::MCMSetting() {}

    MCMSetting::MCMSetting(float defaultValue, std::string exportValue) : defaultValue{defaultValue}, exportValue {exportValue}{}

    void MCMSetting::setup(uint32_t formID) {
        globalVariable = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(formID, "OStim.esp");
    }

    void MCMSetting::restoreDefault() {
        globalVariable->value = defaultValue;
    }

    float MCMSetting::asFloat() {
        return globalVariable->value;
    }

    int MCMSetting::asInt() {
        return static_cast<int>(globalVariable->value);
    }

    bool MCMSetting::asBool() {
        return globalVariable->value != 0;
    }

    void MCMSetting::exportSetting(json& json) {
        json[exportValue] = globalVariable->value;
    }

    void MCMSetting::importSetting(json& json) {
        if (json.contains(exportValue)) {
            globalVariable->value = json[exportValue];
        } else {
            globalVariable->value = defaultValue;
        }
    }
}