#pragma once

namespace Toys {
    namespace Settings {
        enum SynchronizationType : uint8_t {
            PLAYER = 0,
            MALE = 1,
            FEMALE = 2,
            SCENE = 3,
            INDIVIDUAL_SYNCHRONIZATION = 4,

            SYNCHRONIZATION_TYPE_COUNT = 5
        };

        namespace SynchronizationTypeUtil {
            std::string toString(SynchronizationType type);
            std::vector<std::string> getStringValues();
        }
    }
}