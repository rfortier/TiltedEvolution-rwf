#pragma once

#include "../../Model/Setting.h"

namespace Settings {
    namespace BaseTypes {
        struct DropDownSettingParams {
            dropDownIndex defaultIndex = 0;
            std::vector<std::string> options;
        };
    }
}