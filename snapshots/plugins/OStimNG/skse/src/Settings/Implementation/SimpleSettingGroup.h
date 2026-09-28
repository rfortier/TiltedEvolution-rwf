#pragma once

#include "../Model/SettingGroup.h"

namespace Settings {
    class SimpleSettingGroup : public SettingGroup {
    public:
        SimpleSettingGroup(std::string name, SettingDisplayOrder displayOrder, std::vector<Setting*> settings);
        ~SimpleSettingGroup();

        virtual bool isDisposable();

        virtual std::string getName();
        virtual SettingDisplayOrder getDisplayOrder();
        
        virtual uint32_t getSettingCount();
        virtual Setting* getSetting(uint32_t index);

    private:
        std::string name;
        SettingDisplayOrder displayOrder;
        std::vector<Setting*> settings;
    };
}