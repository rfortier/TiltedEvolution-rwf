#include "KeyMapSetting.h"

namespace Settings {
    namespace BaseTypes {
        KeyMapSetting::KeyMapSetting(CommonSettingParams params, KeyMapSettingParams keyMapParams)
            : BaseSetting(params, SettingType::KEY_MAP, keyMapParams.defaultKey) {}
    }
}