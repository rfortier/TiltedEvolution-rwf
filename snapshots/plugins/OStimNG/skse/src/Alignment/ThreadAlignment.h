#pragma once

#include "NodeAlignment.h"

namespace Alignment {
    struct ThreadAlignment {
        std::unordered_map<std::string, NodeAlignment> nodes;
    };
}