#include "ActionAttributes.h"

#include "Serial/Manager.h"
#include "Util/StringUtil.h"
#include "Util/VectorUtil.h"

namespace Graph {
    namespace Action {
        void ActionAttributes::mergeTags() {
            for (ActionTag& tag : tags) {
                tag.roles.forEach([&](Graph::Role role, Action::ActionActor& actor) {
                    roles.get(role)->merge(actor);
                    });
            }
        }

        bool ActionAttributes::hasTag(std::string tag) {
            StringUtil::toLower(&tag);
            for (ActionTag& actionTag : tags) {
                if (actionTag.tag == tag) {
                    return true;
                }
            }
            return false;
        }

        bool ActionAttributes::hasAnyTag(std::vector<std::string> tags) {
            for (std::string& tag : tags) {
                if (hasTag(tag)) {
                    return true;
                }
            }
            return false;
        }

        bool ActionAttributes::hasAllTags(std::vector<std::string> tags) {
            for (std::string& tag : tags) {
                if (!hasTag(tag)) {
                    return false;
                }
            }
            return true;
        }

        bool ActionAttributes::hasOnlyTags(std::vector<std::string> tags) {
            for (ActionTag& actionTag : this->tags) {
                if (!VectorUtil::contains(tags, actionTag.tag)) {
                    return false;
                }
            }
            return true;
        }


        float ActionAttributes::getStimulation(Role role, GameAPI::GameActor actor) {
            float stimulation = Serialization::getActionStimulation(role, actor.getBaseFormID(), type);
            return std::isnan(stimulation) ? roles.get(role)->stimulation : stimulation;
        }

        float ActionAttributes::getMaxStimulation(Role role, GameAPI::GameActor actor) {
            float stimulation = Serialization::getActionMaxStimulation(role, actor.getBaseFormID(), type);
            return std::isnan(stimulation) ? roles.get(role)->maxStimulation : stimulation;
        }


        const char* ActionAttributes::getActionID() {
            return type.c_str();
        }

        bool ActionAttributes::hasTag(const char* tag) {
            return hasTag(std::string(tag));
        }

        uint32_t ActionAttributes::getTagCount() {
            return tags.size();
        }

        OStim::ActionTag* ActionAttributes::getTag(uint32_t index) {
            if (index < 0 || index >= tags.size()) {
                return nullptr;
            }

            return &tags[index];
        }

        void ActionAttributes::forEachTag(OStim::ActionTagVisitor* visitor) {
            for (ActionTag& tag : tags) {
                if (!visitor->visit(&tag)) {
                    break;
                }
            }
        }

        OStim::ActionActor* ActionAttributes::getActor(OStim::Role role) {
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