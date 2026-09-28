#pragma once

#include "ActionActor.h"
#include "../RoleMap.h"

#include "PluginInterface/Graph/ActionTag.h"

namespace Graph {
    namespace Action {
        struct ActionTag : public OStim::ActionTag {
        public:
            std::string tag;
            RoleMap<ActionActor> roles;

            virtual const char* getTagID() override;

            virtual OStim::ActionActor* getActor(OStim::Role role) override;
        };
    }
}