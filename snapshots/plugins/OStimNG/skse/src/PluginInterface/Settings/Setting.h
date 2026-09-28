#pragma once

#include "SettingType.h"

// all setters return a bool value indicating if the whole menu needs to be redrawn
namespace OStim {
    class Setting {
    public:
        virtual bool isDisposable() = 0;

        virtual const char* getName() = 0;
        virtual const char* getTooltip() = 0;
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
        virtual uint32_t getDefaultIndex() = 0;
        virtual uint32_t getCurrentIndex() = 0;
        virtual const char* getCurrentOption() = 0;
        virtual uint32_t getOptionCount() = 0;
        virtual const char* getOption(uint32_t index) = 0;
        virtual bool setIndex(uint32_t index) = 0;

        // text input options
        virtual const char* getDefaultText() = 0;
        virtual const char* getCurrentText() = 0;
        virtual bool setText(const char* text) = 0;

        // key map option
        virtual uint32_t getDefaultKey() = 0;
        virtual uint32_t getCurrentKey() = 0;
        virtual bool setKey(uint32_t key) = 0;

        // button options
        virtual bool click() = 0;


        virtual ~Setting() = 0;
    };

    inline Setting::~Setting() {}
}