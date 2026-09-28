#include "ActionDataScript.h"

#include "Graph/GraphTable.h"
#include "Trait/Condition.h"

namespace ScriptAPI {
    namespace ActionData {
        bool fulfillsActorConditions(std::string actionType, GameAPI::GameActor actor) {
            Graph::Action::ActionAttributes* action = Graph::GraphTable::getActionAttributesByType(actionType);
            if (!action) {
                return false;
            }

            Trait::ActorCondition condition = Trait::ActorCondition::create(actor);
            return condition.fulfills(action->roles.actor.requirements);
        }

        bool fulfillsTargetConditions(std::string actionType, GameAPI::GameActor actor) {
            Graph::Action::ActionAttributes* action = Graph::GraphTable::getActionAttributesByType(actionType);
            if (!action) {
                return false;
            }

            Trait::ActorCondition condition = Trait::ActorCondition::create(actor);
            return condition.fulfills(action->roles.target.requirements);
        }

        bool fulfillsPerformerConditions(std::string actionType, GameAPI::GameActor actor) {
            Graph::Action::ActionAttributes* action = Graph::GraphTable::getActionAttributesByType(actionType);
            if (!action) {
                return false;
            }

            Trait::ActorCondition condition = Trait::ActorCondition::create(actor);
            return condition.fulfills(action->roles.performer.requirements);
        }
    }
}