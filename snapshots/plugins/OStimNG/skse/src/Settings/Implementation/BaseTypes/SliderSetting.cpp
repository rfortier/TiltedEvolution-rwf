#include "SliderSetting.h"

namespace Settings {
    namespace BaseTypes {
        SliderSetting::SliderSetting(CommonSettingParams params, SliderSettingParams sliderParams)
            : BaseSetting(params, SettingType::SLIDER, sliderParams.defaultValue),
              valueStep{sliderParams.valueStep},
              minValue{sliderParams.minValue},
              maxValue{sliderParams.maxValue} {}

        float SliderSetting::getValueStep() {
            return valueStep;
        }

        float SliderSetting::getMinValue() {
            return minValue;
        }

        float SliderSetting::getMaxValue() {
            return maxValue;
        }
    }
}