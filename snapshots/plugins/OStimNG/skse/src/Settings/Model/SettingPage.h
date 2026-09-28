#pragma once

#include "SettingGroup.h"

namespace Settings {
    class SettingPage {
    public:
        virtual void onMenuOpened() = 0;
        virtual std::string getName() = 0;
        virtual SettingDisplayOrder getDisplayOrder() = 0;

        virtual settingGroupIndex getGroupCount() = 0;
        virtual SettingGroup* getGroup(settingGroupIndex index) = 0;
    };
}  // namespace OStim