#pragma once

namespace UI::Align {
    enum class IncrementValue {
        TEN,
        ONE,
        DECIMAL_ONE,
        DECIMAL_TWO,
        DECIMAL_THREE
    };

    class IncrementValueImpl {
    public:
        static float getValue(IncrementValue value);
        static std::string format(IncrementValue value);
        static IncrementValue loop(IncrementValue value);
    };
}