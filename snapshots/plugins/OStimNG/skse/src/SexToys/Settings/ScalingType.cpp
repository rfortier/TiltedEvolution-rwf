#include "ScalingType.h"

namespace Toys {
    namespace Settings {
        namespace ScalingTypeUtil {
            std::string toString(ScalingType type) {
                switch (type) {
                    case ScalingType::NONE:
                        return "$ostim_scaling_type_none";
                    case ScalingType::STIMULATION_RATE:
                        return "$ostim_scaling_type_stimulation_rate";
                    case ScalingType::STIMULATION_VALUE:
                        return "$ostim_scaling_type_stimulation_value";
                    case ScalingType::SPEED:
                        return "$ostim_scaling_type_speed";
                    case ScalingType::INDIVIDUAL_SCALING:
                        return "$ostim_scaling_type_individual";
                    default:
                        return "$ostim_scaling_type_unknown";
                }
            }

            std::vector<std::string> getStringValues() {
                std::vector<std::string> values;
                for (int i = 0; i < ScalingType::SCALING_TYPE_COUNT; i++) {
                    values.push_back(toString(static_cast<ScalingType>(i)));
                }
                return values;
            }
        }
    }
}