#pragma once

#include "../Model/SettingPage.h"

namespace Settings {
    class SimpleSettingPage : public SettingPage {
    public:
        SimpleSettingPage(std::string name, SettingDisplayOrder displayOrder, std::vector<SettingGroup*> groups);
        ~SimpleSettingPage();

        virtual void onMenuOpened();
        virtual std::string getName();
        virtual SettingDisplayOrder getDisplayOrder();

        virtual settingGroupIndex getGroupCount();
        virtual SettingGroup* getGroup(settingGroupIndex index);

    private:
        std::string name;
        SettingDisplayOrder displayOrder;
        std::vector<SettingGroup*> groups;
    };
}