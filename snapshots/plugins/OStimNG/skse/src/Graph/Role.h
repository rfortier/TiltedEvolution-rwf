#pragma once

namespace Graph {
    enum Role : uint8_t {
        ACTOR = 1 << 0,
        TARGET = 1 << 1,
        PERFORMER = 1 << 2
    };
}