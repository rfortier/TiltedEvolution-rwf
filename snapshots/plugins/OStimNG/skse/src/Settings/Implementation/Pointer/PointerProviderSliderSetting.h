#pragma once

#include "../BaseTypes/SliderSetting.h"

namespace Settings {
    namespace PointerSettings {
        class PointerProviderSliderSetting : public BaseTypes::SliderSetting {
        public:
            PointerProviderSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, std::function<float*()> valueProvider);

            virtual float getCurrentValue();
            virtual bool setValue(float value);

        private:
            std::function<float*()> valueProvider;
        };
    }
}