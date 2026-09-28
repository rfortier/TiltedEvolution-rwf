#pragma once

#include "../BaseTypes/SliderSetting.h"

namespace Settings {
    namespace PointerSettings {
        class PointerSliderSetting : public BaseTypes::SliderSetting {
        public:
            PointerSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, float* value);

            virtual float getCurrentValue();
            virtual bool setValue(float value);

        private:
            float* value;
        };
    }
}