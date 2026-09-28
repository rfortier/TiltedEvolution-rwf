#include "GameVariableKeyMapSetting.h"

namespace Settings {
    namespace GameVariableSettings {
        GameVariableKeyMapSetting::GameVariableKeyMapSetting(BaseTypes::CommonSettingParams params, BaseTypes::KeyMapSettingParams keyMapParams, GameAPI::GameVariable variable)
            : BaseTypes::KeyMapSetting(params, keyMapParams),
              variable{variable} {}
        
        float GameVariableKeyMapSetting::getCurrentValue() {
            return variable.getValue();
        }

        bool GameVariableKeyMapSetting::setValue(float value) {
            variable.setValue(value);
            return shouldRedraw();
        }


        keyMap GameVariableKeyMapSetting::getCurrentKey() {
            return static_cast<keyMap>(variable.getValue());
        }

        bool GameVariableKeyMapSetting::setKey(keyMap key) {
            variable.setValue(key);
            return shouldRedraw();
        }
    }
}