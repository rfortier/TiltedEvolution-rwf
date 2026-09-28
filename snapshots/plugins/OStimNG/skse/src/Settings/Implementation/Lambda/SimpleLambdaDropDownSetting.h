#pragma once

#include "../BaseTypes/DropDownSetting.h"

namespace Settings {
    namespace LambdaSettings {
        class SimpleLambdaDropDownSetting : public BaseTypes::DropDownSetting {
        public:
            SimpleLambdaDropDownSetting(BaseTypes::CommonSettingParams params, BaseTypes::DropDownSettingParams dropDownParams, std::function<dropDownIndex()> valueGetter, std::function<void(dropDownIndex)> valueSetter);

            virtual dropDownIndex getCurrentIndex();
            virtual bool setIndex(dropDownIndex index);

        private:
            std::function<dropDownIndex()> valueGetter;
            std::function<void(dropDownIndex)> valueSetter;
        };
    }
}