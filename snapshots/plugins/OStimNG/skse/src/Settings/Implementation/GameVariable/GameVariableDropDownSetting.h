#pragma once

#include "../BaseTypes/DropDownSetting.h"

#include "GameAPI/GameVariable.h"

namespace Settings {
    namespace GameVariableSettings {
        class GameVariableDropDownSetting : public BaseTypes::DropDownSetting {
        public:
            GameVariableDropDownSetting(BaseTypes::CommonSettingParams params, BaseTypes::DropDownSettingParams dropDownParams, GameAPI::GameVariable variable);

            virtual float getCurrentValue();
            virtual bool setValue(float value);

            virtual dropDownIndex getCurrentIndex();
            virtual bool setIndex(dropDownIndex index);

        private:
            GameAPI::GameVariable variable;
        };
    }
}