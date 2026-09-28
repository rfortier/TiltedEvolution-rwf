#pragma once

#include "BaseSetting.h"
#include "ToggleSettingParams.h"

namespace Settings {
    namespace BaseTypes {
        class ToggleSetting : public BaseSetting {
        public:
            ToggleSetting(CommonSettingParams params, ToggleSettingParams toggleParams);
        };
    }
}