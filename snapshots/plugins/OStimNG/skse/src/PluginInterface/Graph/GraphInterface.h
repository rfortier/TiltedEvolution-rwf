#pragma once

#include "ActionTag.h"
#include "ActionType.h"

#include "../PluginInterface.h"

namespace OStim {
    class GraphInterface : public PluginInterface {
    public:
        inline static const char* NAME = "Graph";

        // --- ABI version 0x07050003 ---
        virtual ActionTag* getActionTag(const char* id) = 0;
        virtual ActionType* getActionType(const char* id) = 0;
    };
}