#pragma once

#include "../BaseTypes/ToggleSetting.h"

namespace Settings {
    namespace PointerSettings {
        class PointerToggleSetting : public BaseTypes::ToggleSetting {
        public:
            PointerToggleSetting(BaseTypes::CommonSettingParams params, BaseTypes::ToggleSettingParams toggleParams, bool* value);

            virtual bool isActivated();
            virtual bool toggle();

        private:
            bool* value;
        };
    }
}