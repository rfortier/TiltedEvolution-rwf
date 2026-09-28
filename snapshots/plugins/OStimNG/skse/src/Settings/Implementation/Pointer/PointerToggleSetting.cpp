#include "PointerToggleSetting.h"

namespace Settings {
    namespace PointerSettings {
        PointerToggleSetting::PointerToggleSetting(BaseTypes::CommonSettingParams params, BaseTypes::ToggleSettingParams toggleParams, bool* value)
            : BaseTypes::ToggleSetting(params, toggleParams), value{value} {}

        bool PointerToggleSetting::isActivated() {
            return *value;
        }

        bool PointerToggleSetting::toggle() {
            *value = !*value;
            return shouldRedraw();
        }
    }
}