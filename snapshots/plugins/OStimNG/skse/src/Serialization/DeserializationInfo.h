#pragma once

#include "GameAPI/GameSerializationInterface.h"

namespace Serialization {
    struct DeserializationInfo {
        GameAPI::GameSerializationInterface serial;
        uint32_t version = 0;
    };
}