#include "Core.h"

#include "ThreadManager.h"

#include "ActorProperties/ActorPropertyTable.h"
#include "MCM/MCMTable.h"
#include "Trait/TraitTable.h"
#include "Util/ActorUtil.h"
#include "Util/APITable.h"

namespace Threading {
    void freeActor(GameAPI::GameActor actor, bool byGameLoad) {
        if (byGameLoad) {
            if (!MCM::MCMTable::isScalingDisabled()) {
                actor.setScale(1.0);
            }
            // TODO: clear potential heel offset
        }

        Util::APITable::getActorCountFaction().remove(actor);
        Util::APITable::getExcitementFaction().remove(actor);
        Util::APITable::getInWaterFaction().remove(actor);
        Util::APITable::getSchlongifiedFaction().remove(actor);
        Util::APITable::getTimesClimaxedFaction().remove(actor);
        Util::APITable::getTimeUntilClimaxFaction().remove(actor);
        actor.unlock();
        
        actor.updateAI();

        actor.playAnimation("SOSFlaccid");
    }

    bool isEligible(GameAPI::GameActor actor) {
        return !actor.isDisabled() && !actor.isDeleted() && !actor.isChild() && !actor.isDead() && ActorProperties::ActorPropertyTable::getActorType(actor) != "" && !ThreadManager::GetSingleton()->findActor(actor);
    }
}