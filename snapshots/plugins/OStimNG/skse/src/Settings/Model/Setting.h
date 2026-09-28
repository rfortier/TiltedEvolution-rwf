#pragma once

#include "SettingType.h"

using settingIndex = uint32_t;
using dropDownIndex = uint32_t;
using keyMap = uint32_t;

namespace Settings {
    class Setting {
    public:
        virtual bool isDisposable() = 0;

        virtual std::string getName() = 0;
        virtual std::string getTooltip() = 0;
        virtual SettingType getType() = 0;
        virtual bool isEnabled() = 0;

        // toggle options
        virtual bool isActivatedByDefault() = 0;
        virtual bool isActivated() = 0;
        virtual bool toggle() = 0;

        // slider options
        virtual float getDefaultValue() = 0;
        virtual float getCurrentValue() = 0;
        virtual float getValueStep() = 0;
        virtual float getMinValue() = 0;
        virtual float getMaxValue() = 0;
        virtual bool setValue(float value) = 0;

        // drop down options
        virtual dropDownIndex getDefaultIndex() = 0;
        virtual dropDownIndex getCurrentIndex() = 0;
        virtual std::string getCurrentOption() = 0;
        virtual std::vector<std::string> getOptions() = 0;
        virtual bool setIndex(dropDownIndex index) = 0;

        // text input options
        virtual std::string getDefaultText() = 0;
        virtual std::string getCurrentText() = 0;
        virtual bool setText(std::string text) = 0;

        // key map option
        virtual keyMap getDefaultKey() = 0;
        virtual keyMap getCurrentKey() = 0;
        virtual bool setKey(keyMap key) = 0;

        // button options
        virtual bool click() = 0;
    };
}  // namespace OStim