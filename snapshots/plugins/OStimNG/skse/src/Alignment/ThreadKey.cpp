#include "ThreadKey.h"

namespace Alignment {
    std::string ThreadKey::toString() {
        std::string ret = "";

        for (ActorKey key : keys) {
            if (ret != "") {
                ret += "&";
            }

            ret += key.toString();
        }

        return ret;
    }
}