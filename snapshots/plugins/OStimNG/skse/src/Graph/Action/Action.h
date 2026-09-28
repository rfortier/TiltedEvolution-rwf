#pragma once

#include "ActionAttributes.h"

#include "GameAPI/GameFaction.h"
#include "PluginInterface/Graph/Action.h"

using actionIndex = uint8_t;

namespace Graph {
    namespace Action {
        struct Action : public OStim::Action {
        public:
            actionIndex index = 0;
            std::string type = "";
            ActionAttributes* attributes = nullptr;
            RoleMap<int> roles = {-1, -1, -1};
            bool muted = false;
            bool doPeaks = true;
            bool peaksAnnotated = false;

            bool doFullStrip(int position);
            uint32_t getStrippingMask(int position);
            bool isType(std::string type);
            bool isType(std::vector<std::string> types);

#pragma region ABI
            virtual OStim::ActionType* getType() override;

            virtual int32_t getActor(OStim::Role role) override;
#pragma endregion
        };
    }
}