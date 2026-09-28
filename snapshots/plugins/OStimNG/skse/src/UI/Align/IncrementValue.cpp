#include "IncrementValue.h"

namespace UI::Align {
    float IncrementValueImpl::getValue(IncrementValue value) {
        switch (value) {
            case IncrementValue::TEN:
                return 10;
            case IncrementValue::ONE:
                return 1;
            case IncrementValue::DECIMAL_ONE:
                return 0.1;
            case IncrementValue::DECIMAL_TWO:
                return 0.01;
            case IncrementValue::DECIMAL_THREE:
                return 0.001;
            default:
                return 0;
        }
    }

    std::string IncrementValueImpl::format(IncrementValue value) {
        switch (value) {
            case IncrementValue::TEN:
                return "10.00";
            case IncrementValue::ONE:
                return "1.000";
            case IncrementValue::DECIMAL_ONE:
                return "0.100";
            case IncrementValue::DECIMAL_TWO:
                return "0.010";
            case IncrementValue::DECIMAL_THREE:
                return "0.001";
            default:
                return "0.000";
        }
    }

    IncrementValue IncrementValueImpl::loop(IncrementValue value) {
        switch (value) {
            case IncrementValue::TEN:
                return IncrementValue::ONE;
            case IncrementValue::ONE:
                return IncrementValue::DECIMAL_ONE;
            case IncrementValue::DECIMAL_ONE:
                return IncrementValue::DECIMAL_TWO;
            case IncrementValue::DECIMAL_TWO:
                return IncrementValue::DECIMAL_THREE;
            case IncrementValue::DECIMAL_THREE:
                return IncrementValue::TEN;
            default:
                return IncrementValue::ONE;
        }
    }
}