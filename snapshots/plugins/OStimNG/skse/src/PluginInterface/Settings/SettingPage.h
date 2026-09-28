#pragma once

#include "SettingGroup.h"

namespace OStim {
    class SettingPage {
    public:
        virtual void onMenuOpened() = 0;
        virtual const char* getName() = 0;
        virtual SettingDisplayOrder getDisplayOrder() = 0;

        virtual uint32_t getGroupCount() = 0;
        virtual SettingGroup* getGroup(uint32_t index) = 0;
    };
}