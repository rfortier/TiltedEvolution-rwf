#pragma once

namespace Graph {
    // when adding to this enum also add them to the map in GraphTable.h
    // these don't get serialized anywhere so you can safely change the order
    enum Requirement {
        NONE =      0,
        ANUS =      1 << 0,
        BREAST =    1 << 1,
        FOOT =      1 << 2,
        HAND =      1 << 3,
        MOUTH =     1 << 4,
        NIPPLE =    1 << 5,
        PENIS =     1 << 6,
        TESTICLES = 1 << 7,
        VAGINA =    1 << 8,
        VAMPIRE =   1 << 9,
    };
}