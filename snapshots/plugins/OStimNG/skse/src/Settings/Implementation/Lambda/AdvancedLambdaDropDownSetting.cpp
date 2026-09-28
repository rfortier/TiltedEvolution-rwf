#include "AdvancedLambdaDropDownSetting.h"

namespace Settings {
    namespace LambdaSettings {
        AdvancedLambdaDropDownSetting::AdvancedLambdaDropDownSetting(BaseTypes::CommonSettingParams params, int defaultIndex, std::function<dropDownIndex()> valueGetter, std::function<void(dropDownIndex)> valueSetter, std::function<dropDownIndex()> optionCountProvider, std::function<std::string(dropDownIndex)> optionNameProvider)
            : BaseTypes::BaseSetting(params, SettingType::DROP_DOWN, defaultIndex),
              valueGetter{valueGetter},
              valueSetter {valueSetter},
              optionCountProvider{optionCountProvider},
              optionNameProvider{optionNameProvider} {}

        dropDownIndex AdvancedLambdaDropDownSetting::getCurrentIndex() {
            return valueGetter();
        }

        std::string AdvancedLambdaDropDownSetting::getCurrentOption() {
            return optionNameProvider(getCurrentIndex());
        }

        std::vector<std::string> AdvancedLambdaDropDownSetting::getOptions() {
            std::vector<std::string> options;
            dropDownIndex count = optionCountProvider();
            for (int i = 0; i < count; i++) {
                options.push_back(optionNameProvider(i));
            }
            return options;
        }

        bool AdvancedLambdaDropDownSetting::setIndex(dropDownIndex index) {
            valueSetter(index);
            return shouldRedraw();
        }
    }
}