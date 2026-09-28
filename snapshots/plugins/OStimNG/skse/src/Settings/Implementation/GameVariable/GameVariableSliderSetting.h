#pragma once

#include "../BaseTypes/SliderSetting.h"

#include "GameAPI/GameVariable.h"

namespace Settings {
    namespace GameVariableSettings {
        class GameVariableSliderSetting : public BaseTypes::SliderSetting {
        public:
            GameVariableSliderSetting(BaseTypes::CommonSettingParams params, BaseTypes::SliderSettingParams sliderParams, GameAPI::GameVariable variable);

            virtual float getCurrentValue();
            virtual bool setValue(float value);

        private:
            GameAPI::GameVariable variable;
        };
    }
}