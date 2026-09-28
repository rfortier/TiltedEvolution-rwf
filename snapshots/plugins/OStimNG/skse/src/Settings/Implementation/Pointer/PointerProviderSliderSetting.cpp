#include "PointerProviderSliderSetting.h"

namespace Settings {
    namespace PointerSettings {
        PointerProviderSliderSetting::PointerProviderSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, std::function<float*()> valueProvider)
            : BaseTypes::SliderSetting(params, sliderParams),
              valueProvider{valueProvider} {}

        float PointerProviderSliderSetting::getCurrentValue() {
            return *valueProvider();
        }

        bool PointerProviderSliderSetting::setValue(float value) {
            *valueProvider() = value;
            return shouldRedraw();
        }
    }
}