#pragma once

#include "../BaseTypes/KeyMapSetting.h"

#include "GameAPI/GameVariable.h"

namespace Settings {
    namespace GameVariableSettings {
        class GameVariableKeyMapSetting : public BaseTypes::KeyMapSetting {
        public:
            GameVariableKeyMapSetting(BaseTypes::CommonSettingParams params, BaseTypes::KeyMapSettingParams keyMapParams, GameAPI::GameVariable variable);

            virtual float getCurrentValue();
            virtual bool setValue(float value);

            virtual keyMap getCurrentKey();
            virtual bool setKey(keyMap key);

        private:
            GameAPI::GameVariable variable;
        };
    }
}