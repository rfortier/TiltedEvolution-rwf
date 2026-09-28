#pragma once

#include "MinMaxSliderSettingParams.h"
#include "SliderSetting.h"

namespace Settings {
    namespace BaseTypes {
        class MinMaxSliderSetting : public SliderSetting {
        public:
            MinMaxSliderSetting(CommonSettingParams params, SliderSettingParams sliderParams, MinMaxSliderSettingParams minMaxParams);

            virtual float getCurrentValue();
            virtual bool setValue(float value);

            virtual float getMinSettingValue() = 0;
            virtual float getMaxSettingValue() = 0;
            virtual void setMinSettingValue(float value) = 0;
            virtual void setMaxSettingValue(float value) = 0;

        private:
            bool isMinSetting;
        };
    }
}