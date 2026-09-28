#include "PluginSetting.h"

namespace Settings {
    namespace PluginSettings {
        PluginSetting::PluginSetting(OStim::Setting* setting, bool disposable)
            : setting{setting}, disposable{disposable} {}

        PluginSetting::~PluginSetting() {
            if (setting->isDisposable()) {
                delete setting;
            }
        }


        bool PluginSetting::isDisposable() {
            return disposable;
        }


        std::string PluginSetting::getName() {
            return std::string(setting->getName());
        }

        std::string PluginSetting::getTooltip() {
            return std::string(setting->getTooltip());
        }

        SettingType PluginSetting::getType() {
            return static_cast<SettingType>(setting->getType());
        }

        bool PluginSetting::isEnabled() {
            return setting->isEnabled();
        }


        bool PluginSetting::isActivatedByDefault() {
            return setting->isActivatedByDefault();
        }

        bool PluginSetting::isActivated() {
            return setting->isActivated();
        }

        bool PluginSetting::toggle() {
            return setting->toggle();
        }


        float PluginSetting::getDefaultValue() {
            return setting->getDefaultValue();
        }

        float PluginSetting::getCurrentValue() {
            return setting->getCurrentValue();
        }

        float PluginSetting::getValueStep() {
            return setting->getValueStep();
        }

        float PluginSetting::getMinValue() {
            return setting->getMinValue();
        }

        float PluginSetting::getMaxValue() {
            return setting->getMaxValue();
        }

        bool PluginSetting::setValue(float value) {
            return setting->setValue(value);
        }


        dropDownIndex PluginSetting::getDefaultIndex() {
            return setting->getDefaultIndex();
        }

        dropDownIndex PluginSetting::getCurrentIndex() {
            return setting->getCurrentIndex();
        }

        std::string PluginSetting::getCurrentOption() {
            return std::string(setting->getCurrentOption());
        }

        std::vector<std::string> PluginSetting::getOptions() {
            uint32_t count = setting->getOptionCount();
            std::vector<std::string> options(count);
            for (int i = 0; i < count; i++) {
                options.push_back(std::string(setting->getOption(i)));
            }
            return options;
        }

        bool PluginSetting::setIndex(dropDownIndex index) {
            return setting->setIndex(index);
        }


        std::string PluginSetting::getDefaultText() {
            return std::string(setting->getDefaultText());
        }

        std::string PluginSetting::getCurrentText() {
            return std::string(setting->getCurrentText());
        }

        bool PluginSetting::setText(std::string text) {
            return setting->setText(text.c_str());
        }


        keyMap PluginSetting::getDefaultKey() {
            return setting->getDefaultKey();
        }

        keyMap PluginSetting::getCurrentKey() {
            return setting->getCurrentKey();
        }

        bool PluginSetting::setKey(keyMap key) {
            return setting->setKey(key);
        }


        bool PluginSetting::click() {
            return setting->click();
        }
    }
}