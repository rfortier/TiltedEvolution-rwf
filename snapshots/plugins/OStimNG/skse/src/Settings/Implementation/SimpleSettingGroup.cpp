#include "SimpleSettingGroup.h"

namespace Settings {
    SimpleSettingGroup::SimpleSettingGroup(std::string name, SettingDisplayOrder displayOrder, std::vector<Setting*> settings)
        : name{name}, displayOrder{displayOrder}, settings{settings} {}

    SimpleSettingGroup::~SimpleSettingGroup() {
        for (Setting* setting : settings) {
            delete setting;
        }
    }


    bool SimpleSettingGroup::isDisposable() {
        return false;
    }

    std::string SimpleSettingGroup::getName() {
        return name;
    }

    SettingDisplayOrder SimpleSettingGroup::getDisplayOrder() {
        return displayOrder;
    }

    uint32_t SimpleSettingGroup::getSettingCount() {
        return settings.size();
    }

    Setting* SimpleSettingGroup::getSetting(uint32_t index) {
        if (index < 0 || index >= settings.size()) {
            return nullptr;
        }

        return settings[index];
    }
}