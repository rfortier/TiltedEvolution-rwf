#pragma once

#include "../BaseTypes/BaseSetting.h"

namespace Settings {
    namespace LambdaSettings {
        class AdvancedLambdaDropDownSetting : public BaseTypes::BaseSetting {
        public:
            AdvancedLambdaDropDownSetting(BaseTypes::CommonSettingParams params, int defaultIndex, std::function<dropDownIndex()> valueGetter, std::function<void(dropDownIndex)> valueSetter, std::function<dropDownIndex()> optionCountProvider, std::function<std::string(dropDownIndex)> optionNameProvider);

            virtual dropDownIndex getCurrentIndex();
            virtual std::string getCurrentOption();
            virtual std::vector<std::string> getOptions();
            virtual bool setIndex(dropDownIndex index);

        private:
            std::function<dropDownIndex()> valueGetter;
            std::function<void(dropDownIndex)> valueSetter;
            std::function<dropDownIndex()> optionCountProvider;
            std::function<std::string(dropDownIndex)> optionNameProvider;
        };
    }
}