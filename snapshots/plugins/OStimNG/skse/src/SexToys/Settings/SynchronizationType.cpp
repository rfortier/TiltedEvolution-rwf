#include "SynchronizationType.h"

namespace Toys {
    namespace Settings {
        namespace SynchronizationTypeUtil {
            std::string toString(SynchronizationType type) {
                switch (type) {
                    case SynchronizationType::PLAYER:
                        return "$ostim_synchronization_type_player";
                    case SynchronizationType::MALE:
                        return "$ostim_synchronization_type_male";
                    case SynchronizationType::FEMALE:
                        return "$ostim_synchronization_type_female";
                    case SynchronizationType::SCENE:
                        return "$ostim_synchronization_type_scene";
                    case SynchronizationType::INDIVIDUAL_SYNCHRONIZATION:
                        return "$ostim_synchronization_type_individual";
                    default:
                        return "$ostim_synchronization_type_unknown";
                }
            }

            std::vector<std::string> getStringValues() {
                std::vector<std::string> values;
                for (int i = 0; i < SynchronizationType::SYNCHRONIZATION_TYPE_COUNT; i++) {
                    values.push_back(toString(static_cast<SynchronizationType>(i)));
                }
                return values;
            }
        }
    }
}