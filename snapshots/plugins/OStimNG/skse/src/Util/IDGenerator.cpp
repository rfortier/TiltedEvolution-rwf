#include "IDGenerator.h"

namespace Util {
    int IDGenerator::get() {
        if (freeIDs.empty()) {
            return next++;
        }

        int id = freeIDs.front();
        freeIDs.pop();
        return id;
    }

    void IDGenerator::reset() {
        while (!freeIDs.empty()) {
            freeIDs.pop();
        }

        next = start;
    }
}