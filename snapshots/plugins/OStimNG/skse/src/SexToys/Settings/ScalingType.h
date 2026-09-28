#pragma once

namespace Toys {
    namespace Settings {
        enum ScalingType : uint8_t {
            NONE = 0,
            STIMULATION_RATE = 1,
            STIMULATION_VALUE = 2,
            SPEED = 3,
            INDIVIDUAL_SCALING = 4,

            SCALING_TYPE_COUNT = 5
        };

        namespace ScalingTypeUtil {
            std::string toString(ScalingType type);
            std::vector<std::string> getStringValues();
        }
    }
}