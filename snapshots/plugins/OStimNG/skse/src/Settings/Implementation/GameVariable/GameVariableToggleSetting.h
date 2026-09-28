#pragma once

#include "../BaseTypes/ToggleSetting.h"

#include "GameAPI/GameVariable.h"

namespace Settings {
    namespace GameVariableSettings {
        class GameVariableToggleSetting : public BaseTypes::ToggleSetting {
        public:
            GameVariableToggleSetting(BaseTypes::CommonSettingParams params, BaseTypes::ToggleSettingParams toggleParams, GameAPI::GameVariable variable);

            virtual bool isActivated();
            virtual bool toggle();

            virtual float getCurrentValue();
            virtual bool setValue(float value);

        private:
            GameAPI::GameVariable variable;
        };
    }
}