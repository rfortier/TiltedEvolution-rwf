#pragma once

#include "InterfaceMap.h"

namespace OStim {
    struct InterfaceExchangeMessage {
        enum : uint32_t {
            MESSAGE_TYPE = 'OST'
        };

        InterfaceMap* interfaceMap = NULL;
    };
}