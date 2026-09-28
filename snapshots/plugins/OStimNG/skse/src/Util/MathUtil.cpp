#include "MathUtil.h"

namespace MathUtil {
    int32_t uintToInt(uint32_t value) {
        int32_t ret;
        std::memcpy(&ret, &value, sizeof(ret));
        return ret;
    }

    uint32_t intToUint(int32_t value) {
        uint32_t ret;
        std::memcpy(&ret, &value, sizeof(ret));
        return ret;
    }
}