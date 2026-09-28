#pragma once

namespace Settings {
    namespace BaseTypes {
        struct SliderSettingParams {
            float defaultValue = 0.0f;
            float valueStep = 0.0f;
            float minValue = 0.0f;
            float maxValue = 0.0f;
        };
    }
}