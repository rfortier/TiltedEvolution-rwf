#pragma once

#include "../../Model/Setting.h"
#include "CommonSettingParams.h"

namespace Settings {
    namespace BaseTypes {
        class BaseSetting : public Setting {
        public:
            BaseSetting(CommonSettingParams params, SettingType type, float defaultValue);

            virtual bool isDisposable() override;

            virtual std::string getName() override;
            virtual std::string getTooltip() override;
            virtual SettingType getType() override;
            virtual bool isEnabled() override;

            virtual bool isActivatedByDefault() override;
            virtual bool isActivated() override;
            virtual bool toggle() override;

            virtual float getDefaultValue() override;
            virtual float getCurrentValue() override;
            virtual float getValueStep() override;
            virtual float getMinValue() override;
            virtual float getMaxValue() override;
            virtual bool setValue(float value) override;

            virtual dropDownIndex getDefaultIndex() override;
            virtual dropDownIndex getCurrentIndex() override;
            virtual std::string getCurrentOption() override;
            virtual std::vector<std::string> getOptions() override;
            virtual bool setIndex(dropDownIndex index) override;

            virtual std::string getDefaultText() override;
            virtual std::string getCurrentText() override;
            virtual bool setText(std::string text) override;

            virtual keyMap getDefaultKey() override;
            virtual keyMap getCurrentKey() override;
            virtual bool setKey(keyMap key) override;

            virtual bool click() override;

        protected:
            inline bool shouldRedraw() { return alwaysRedraw; }

        private:
            std::string name;
            std::string tooltip;
            SettingType type;
            std::function<bool()> enabled;
            bool alwaysRedraw;
            float defaultValue;
        };
    }
}