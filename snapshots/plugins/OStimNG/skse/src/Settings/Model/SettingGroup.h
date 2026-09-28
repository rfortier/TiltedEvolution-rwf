#pragma once

#include "Setting.h"
#include "SettingDisplayOrder.h"

using settingGroupIndex = uint32_t;

namespace Settings {
    class SettingGroup {
    public:
        virtual bool isDisposable() = 0;

        virtual std::string getName() = 0;
        virtual SettingDisplayOrder getDisplayOrder() = 0;

        virtual settingIndex getSettingCount() = 0;
        virtual Setting* getSetting(settingIndex index) = 0;
    };
}  // namespace OStim