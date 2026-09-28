#pragma once

#include "../BaseTypes/MinMaxSliderSetting.h"

namespace Settings {
    namespace PointerSettings {
        class PointerProviderMinMaxSliderSetting : public BaseTypes::MinMaxSliderSetting {
        public:
            PointerProviderMinMaxSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, BaseTypes::MinMaxSliderSettingParams minMaxParams, std::function<float*()> minValueProvider, std::function<float*()> maxValueProvider);

            virtual float getMinSettingValue();
            virtual float getMaxSettingValue();
            virtual void setMinSettingValue(float value);
            virtual void setMaxSettingValue(float value);

        private:
            std::function<float*()> minValueProvider;
            std::function<float*()> maxValueProvider;
        };
    }
}