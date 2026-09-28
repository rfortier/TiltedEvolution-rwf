#include "Action.h"

#include "../GraphTable.h"

namespace Graph {
    namespace Action {
        bool Action::doFullStrip(int position) {
            bool doStrip = false;
            roles.forEach([this, position, &doStrip](Role role, int index) {
                doStrip |= index == position && attributes->roles.get(role)->fullStrip;
            });
            return doStrip;
        }

        uint32_t Action::getStrippingMask(int position) {
            int mask = 0;
            roles.forEach([this, position, &mask](Role role, int index) {
                mask |= index == position ? attributes->roles.get(role)->strippingMask : 0;
            });
            return mask;
        }

        bool Action::isType(std::string type) {
            return this->type == GraphTable::getActionAlias(type);
        }

        bool Action::isType(std::vector<std::string> types) {
            for (std::string& type : types) {
                if (isType(type)) {
                    return true;
                }
            }
            return false;
        }


        OStim::ActionType* Action::getType(){
            return attributes;
        }

        int32_t Action::getActor(OStim::Role role) {
            return *roles.get(static_cast<Role>(role));
        }
    }
}