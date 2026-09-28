#include "PointerSliderSetting.h"

namespace Settings {
    namespace PointerSettings {
        PointerSliderSetting::PointerSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, float* value)
            : BaseTypes::SliderSetting(params, sliderParams),
              value{value} {}

        float PointerSliderSetting::getCurrentValue() {
            return *value;
        }

        bool PointerSliderSetting::setValue(float value) {
            *this->value = value;
            return shouldRedraw();
        }
    }
}