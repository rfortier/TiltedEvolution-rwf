#include "MinMaxSliderSetting.h"

namespace Settings {
    namespace BaseTypes {
        MinMaxSliderSetting::MinMaxSliderSetting(CommonSettingParams params, SliderSettingParams sliderParams, MinMaxSliderSettingParams minMaxParams)
            : SliderSetting(params, sliderParams),
              isMinSetting{minMaxParams.isMinSetting} {}

        float MinMaxSliderSetting::getCurrentValue() {
            return isMinSetting ? getMinSettingValue() : getMaxSettingValue();
        }

        bool MinMaxSliderSetting::setValue(float value) {
            if (isMinSetting) {
                if (getMaxSettingValue() < value) {
                    setMinSettingValue(getMaxSettingValue());
                    setMaxSettingValue(value);
                    return true;
                } else {
                    setMinSettingValue(value);
                }
            } else {
                if (getMinSettingValue() > value) {
                    setMaxSettingValue(getMinSettingValue());
                    setMinSettingValue(value);
                    return true;
                } else {
                    setMaxSettingValue(value);
                }
            }
            return shouldRedraw();
        }
    }
}