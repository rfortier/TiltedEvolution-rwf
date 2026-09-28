#include "GameVariableDropDownSetting.h"

namespace Settings {
    namespace GameVariableSettings {
        GameVariableDropDownSetting::GameVariableDropDownSetting(BaseTypes::CommonSettingParams params, BaseTypes::DropDownSettingParams dropDownParams, GameAPI::GameVariable variable)
            : BaseTypes::DropDownSetting(params, dropDownParams),
              variable{variable} {}
        

        float GameVariableDropDownSetting::getCurrentValue() {
            return variable.getValue();
        }

        bool GameVariableDropDownSetting::setValue(float value) {
            variable.setValue(value);
            return shouldRedraw();
        }


        dropDownIndex GameVariableDropDownSetting::getCurrentIndex() {
            return static_cast<dropDownIndex>(variable.getValue());
        }

        bool GameVariableDropDownSetting::setIndex(dropDownIndex index) {
            variable.setValue(index);
            return shouldRedraw();
        }
    }
}