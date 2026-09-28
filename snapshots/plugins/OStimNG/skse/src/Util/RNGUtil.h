#pragma once

namespace RNGUtil {
    inline std::default_random_engine RNG = std::default_random_engine{};

    inline void setup() { RNG.seed((unsigned)time(NULL)); }

    inline int uniformInt(int min, int max) { return std::uniform_int_distribution<>(min, max)(RNG); }
    inline int normalInt(int min, int max) { return std::round(std::normal_distribution<>(min, max)(RNG)); }
    inline bool chanceRoll(int chance) { return chance > uniformInt(0, 99); }

    inline float uniformFloat(float min, float max) { return std::uniform_real_distribution<>(min, max)(RNG); }
}