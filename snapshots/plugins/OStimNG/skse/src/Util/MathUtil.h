#pragma once

#include "Util/Constants.h"

#define _USE_MATH_DEFINES

#include <math.h>

namespace MathUtil {
    inline int clamp(int value, int min, int max) { return std::min(max, std::max(min, value)); }
    inline float toDegrees(float angle) { return angle * 180 / M_PI; }
    inline float toRadians(float angle) { return angle * M_PI / 180; }
    int32_t uintToInt(uint32_t value);
    uint32_t intToUint(int32_t value);
}