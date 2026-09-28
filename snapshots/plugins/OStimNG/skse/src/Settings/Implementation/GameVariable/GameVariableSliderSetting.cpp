#include "GameVariableSliderSetting.h"

namespace Settings {
    namespace GameVariableSettings {
        GameVariableSliderSetting::GameVariableSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, GameAPI::GameVariable variable)
            : BaseTypes::SliderSetting(params, sliderParams),
              variable{variable} {}


        float GameVariableSliderSetting::getCurrentValue() {
            return variable.getValue();
        }

        bool GameVariableSliderSetting::setValue(float value) {
            variable.setValue(value);
            return shouldRedraw();
        }
    }
}