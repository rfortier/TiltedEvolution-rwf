#pragma once

#include "ActorKey.h"

namespace Alignment {
    struct ThreadKey {
        std::vector<ActorKey> keys;

        std::string toString();

        inline bool operator==(ThreadKey other) const {
            if (keys.size() != other.keys.size()) {
                return false;
            }

            for (int i = 0; i < keys.size(); i++) {
                if (keys[i] != other.keys[i]) {
                    return false;
                }
            }

            return true;
        }

        inline bool operator!=(ThreadKey other) const {
            if (keys.size() != other.keys.size()) {
                return true;
            }

            for (int i = 0; i < keys.size(); i++) {
                if (keys[i] != other.keys[i]) {
                    return true;
                }
            }

            return false;
        }
    };
}