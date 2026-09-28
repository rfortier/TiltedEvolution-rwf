#pragma once

#include "../../Model/Setting.h"

#include "PluginInterface/Settings/Setting.h"

namespace Settings {
    namespace PluginSettings {
        class PluginSetting : public Setting {
        public:
            PluginSetting(OStim::Setting* setting, bool disposable);
            ~PluginSetting();

            virtual bool isDisposable() override;

            virtual std::string getName() override;
            virtual std::string getTooltip() override;
            virtual SettingType getType() override;
            virtual bool isEnabled() override;

            // toggle options
            virtual bool isActivatedByDefault() override;
            virtual bool isActivated() override;
            virtual bool toggle() override;

            // slider options
            virtual float getDefaultValue() override;
            virtual float getCurrentValue() override;
            virtual float getValueStep() override;
            virtual float getMinValue() override;
            virtual float getMaxValue() override;
            virtual bool setValue(float value) override;

            // drop down options
            virtual dropDownIndex getDefaultIndex() override;
            virtual dropDownIndex getCurrentIndex() override;
            virtual std::string getCurrentOption() override;
            virtual std::vector<std::string> getOptions() override;
            virtual bool setIndex(dropDownIndex index) override;

            // text input options
            virtual std::string getDefaultText() override;
            virtual std::string getCurrentText() override;
            virtual bool setText(std::string text) override;

            // key map option
            virtual keyMap getDefaultKey() override;
            virtual keyMap getCurrentKey() override;
            virtual bool setKey(keyMap key) override;

            // button options
            virtual bool click() override;

        private:
            OStim::Setting* setting;
            bool disposable;
        };
    }
}