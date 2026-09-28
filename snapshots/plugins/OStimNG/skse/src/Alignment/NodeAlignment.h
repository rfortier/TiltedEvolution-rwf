#pragma once

#include "ActorAlignment.h"

namespace Alignment {
    struct NodeAlignment {
        std::unordered_map<int, ActorAlignment> actors;
    };
}