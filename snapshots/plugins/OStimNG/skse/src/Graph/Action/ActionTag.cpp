#include "ActionTag.h"

namespace Graph {
    namespace Action {
        const char* ActionTag::getTagID() {
            return tag.c_str();
        }

        OStim::ActionActor* ActionTag::getActor(OStim::Role role) {
            switch (role){
            case OStim::Role::ACTOR:
                return &roles.actor;
            case OStim::Role::TARGET:
                return &roles.target;
            case OStim::Role::PERFORMER:
                return &roles.performer;
            default:
                return nullptr;
            }
        }
    }
}