#include "ToggleSetting.h"

namespace Settings {
    namespace BaseTypes {
        ToggleSetting::ToggleSetting(CommonSettingParams params, ToggleSettingParams toggleParams)
            : BaseSetting(params, SettingType::TOGGLE, toggleParams.defaultValue ? 1.0f : 0.0f) {}
    }
}