#include "PointerProviderMinMaxSliderSetting.h"

namespace Settings {
    namespace PointerSettings {
        PointerProviderMinMaxSliderSetting::PointerProviderMinMaxSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, BaseTypes::MinMaxSliderSettingParams minMaxParams, std::function<float*()> minValueProvider, std::function<float*()> maxValueProvider)
            : BaseTypes::MinMaxSliderSetting(params, sliderParams, minMaxParams),
              minValueProvider{minValueProvider},
              maxValueProvider{maxValueProvider} {}

        float PointerProviderMinMaxSliderSetting::getMinSettingValue() {
            return *minValueProvider();
        }

        float PointerProviderMinMaxSliderSetting::getMaxSettingValue() {
            return *maxValueProvider();
        }

        void PointerProviderMinMaxSliderSetting::setMinSettingValue(float value) {
            *minValueProvider() = value;
        }

        void PointerProviderMinMaxSliderSetting::setMaxSettingValue(float value) {
            *maxValueProvider() = value;
        }
    }
}