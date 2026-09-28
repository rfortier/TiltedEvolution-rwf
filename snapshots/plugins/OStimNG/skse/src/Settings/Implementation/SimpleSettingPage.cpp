#include "SimpleSettingPage.h"

namespace Settings {
    SimpleSettingPage::SimpleSettingPage(std::string name, SettingDisplayOrder displayOrder, std::vector<SettingGroup*> groups)
        : name{name}, displayOrder{displayOrder}, groups{groups} {}

    SimpleSettingPage::~SimpleSettingPage() {
        for (SettingGroup* group : groups) {
            delete group;
        }
    }


    void SimpleSettingPage::onMenuOpened() {}

    std::string SimpleSettingPage::getName() {
        return name.c_str();
    }

    SettingDisplayOrder SimpleSettingPage::getDisplayOrder() {
        return displayOrder;
    }

    settingGroupIndex SimpleSettingPage::getGroupCount() {
        return groups.size();
    }

    SettingGroup* SimpleSettingPage::getGroup(settingGroupIndex index) {
        if (index < 0 || index >= groups.size()) {
            return nullptr;
        }

        return groups[index];
    }
}