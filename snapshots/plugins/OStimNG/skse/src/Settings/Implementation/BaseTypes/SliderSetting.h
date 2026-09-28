#pragma once

#include "BaseSetting.h"
#include "SliderSettingParams.h"

namespace Settings {
    namespace BaseTypes {
        class SliderSetting : public BaseSetting {
        public:
            SliderSetting(CommonSettingParams params, SliderSettingParams sliderParams);

            virtual float getValueStep();
            virtual float getMinValue();
            virtual float getMaxValue();

        private:
            float valueStep;
            float minValue;
            float maxValue;
        };
    }
}