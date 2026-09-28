#pragma once

#include "BaseSetting.h"
#include "KeyMapSettingParams.h"

namespace Settings {
    namespace BaseTypes {
        class KeyMapSetting : public BaseSetting {
        public:
            KeyMapSetting(CommonSettingParams params, KeyMapSettingParams keyMapParams);
        };
    }
}