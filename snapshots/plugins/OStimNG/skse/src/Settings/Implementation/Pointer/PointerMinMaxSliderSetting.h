#pragma once

#include "../BaseTypes/MinMaxSliderSetting.h"

namespace Settings {
    namespace PointerSettings {
        class PointerMinMaxSliderSetting : public BaseTypes::MinMaxSliderSetting {
        public:
            PointerMinMaxSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, BaseTypes::MinMaxSliderSettingParams minMaxParams, float* minValue, float* maxValue);

            virtual float getMinSettingValue();
            virtual float getMaxSettingValue();
            virtual void setMinSettingValue(float value);
            virtual void setMaxSettingValue(float value);

        private:
            float* minValue;
            float* maxValue;
        };
    }
}