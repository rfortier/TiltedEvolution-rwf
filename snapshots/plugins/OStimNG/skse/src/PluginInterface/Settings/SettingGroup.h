#pragma once

#include "Setting.h"
#include "SettingDisplayOrder.h"

namespace OStim {
    class SettingGroup {
    public:
        virtual bool isDisposable() = 0;

        virtual const char* getName() = 0;
        virtual SettingDisplayOrder getDisplayOrder() = 0;

        virtual uint32_t getSettingCount() = 0;
        virtual Setting* getSetting(uint32_t index) = 0;


        virtual ~SettingGroup() = 0;
    };

    inline SettingGroup::~SettingGroup() {}
}