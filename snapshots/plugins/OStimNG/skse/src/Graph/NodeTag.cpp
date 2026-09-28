#include "NodeTag.h"

namespace Graph {
    const char* NodeTag::getTagID() {
        return tag.c_str();
    }
}