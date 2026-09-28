#include "SimpleLambdaDropDownSetting.h"

namespace Settings {
    namespace LambdaSettings {
        SimpleLambdaDropDownSetting::SimpleLambdaDropDownSetting(BaseTypes::CommonSettingParams params, BaseTypes::DropDownSettingParams dropDownParams, std::function<dropDownIndex()> valueGetter, std::function<void(dropDownIndex)> valueSetter)
            : DropDownSetting(params, dropDownParams),
              valueGetter{valueGetter},
              valueSetter{valueSetter} {}

        dropDownIndex SimpleLambdaDropDownSetting::getCurrentIndex() {
            return valueGetter();
        }

        bool SimpleLambdaDropDownSetting::setIndex(dropDownIndex index) {
            valueSetter(index);
            return shouldRedraw();
        }
    }
}