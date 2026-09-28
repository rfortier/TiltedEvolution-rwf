#pragma once
#include <cstdint>
#include "InterfaceMap.h"

namespace OStim
{
    struct InterfaceExchangeMessage
    {
        enum : std::uint32_t
        {
            MESSAGE_TYPE = 'OST'
        };

        InterfaceMap* interfaceMap = nullptr;
    };
}
