#pragma once

namespace MCM {
    struct MCMSetting {
    public:
        MCMSetting(); // default constructor is necessary or map access won't compile..
        MCMSetting(float defaultValue, std::string exportValue);

        void setup(uint32_t formID);
        void restoreDefault();
        float asFloat();
        int asInt();
        bool asBool();
        void exportSetting(json& json);
        void importSetting(json& json);

    private:
        RE::TESGlobal* globalVariable;
        float defaultValue;
        std::string exportValue;
    };
}