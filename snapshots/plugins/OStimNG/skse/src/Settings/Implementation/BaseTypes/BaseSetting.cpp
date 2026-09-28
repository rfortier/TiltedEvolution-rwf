#include "BaseSetting.h"

namespace Settings {
    namespace BaseTypes {
        BaseSetting::BaseSetting(CommonSettingParams params, SettingType type, float defaultValue)
            : name{params.name},
              tooltip{params.tooltip},
              type{type},
              enabled{params.enabled},
              alwaysRedraw{params.alwaysRedraw},
              defaultValue{defaultValue} {}

        bool BaseSetting::isDisposable() {
            return false;
        }

        std::string BaseSetting::getName() {
            return name;
        }

        std::string BaseSetting::getTooltip() {
            return tooltip;
        }

        SettingType BaseSetting::getType() {
            return type;
        }

        bool BaseSetting::isEnabled() {
            return enabled();
        }


        bool BaseSetting::isActivatedByDefault() {
            return defaultValue != 0.0f;
        }

        bool BaseSetting::isActivated() {
            return false;
        }

        bool BaseSetting::toggle() {
            return false;
        }


        float BaseSetting::getDefaultValue() {
            return defaultValue;
        }

        float BaseSetting::getCurrentValue() {
            return 0.0f;
        }

        float BaseSetting::getValueStep() {
            return 0.0f;
        }

        float BaseSetting::getMinValue() {
            return 0.0f;
        }

        float BaseSetting::getMaxValue() {
            return 0.0f;
        }

        bool BaseSetting::setValue(float value) {
            return false;
        }


        dropDownIndex BaseSetting::getDefaultIndex() {
            return static_cast<uint32_t>(defaultValue);
        }

        dropDownIndex BaseSetting::getCurrentIndex() {
            return 0;
        }

        std::string BaseSetting::getCurrentOption() {
            return "";
        }

        std::vector<std::string> BaseSetting::getOptions() {
            return {};
        }

        bool BaseSetting::setIndex(dropDownIndex index) {
            return false;
        }


        std::string BaseSetting::getDefaultText() {
            return "";
        }

        std::string BaseSetting::getCurrentText() {
            return "";
        }

        bool BaseSetting::setText(std::string text) {
            return false;
        }


        keyMap BaseSetting::getDefaultKey() {
            return static_cast<keyMap>(defaultValue);
        }

        keyMap BaseSetting::getCurrentKey() {
            return 0;
        }

        bool BaseSetting::setKey(keyMap key) {
            return false;
        }


        bool BaseSetting::click() {
            return false;
        }
    }
}