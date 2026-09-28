#include "PointerMinMaxSliderSetting.h"

namespace Settings {
    namespace PointerSettings {
        PointerMinMaxSliderSetting::PointerMinMaxSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, BaseTypes::MinMaxSliderSettingParams minMaxParams, float* minValue, float* maxValue)
            : BaseTypes::MinMaxSliderSetting(params, sliderParams, minMaxParams),
              minValue{minValue},
              maxValue{maxValue} {}

        float PointerMinMaxSliderSetting::getMinSettingValue() {
            return *minValue;
        }

        float PointerMinMaxSliderSetting::getMaxSettingValue() {
            return *maxValue;
        }

        void PointerMinMaxSliderSetting::setMinSettingValue(float value) {
            *minValue = value;
        }

        void PointerMinMaxSliderSetting::setMaxSettingValue(float value) {
            *maxValue = value;
        }
    }
}