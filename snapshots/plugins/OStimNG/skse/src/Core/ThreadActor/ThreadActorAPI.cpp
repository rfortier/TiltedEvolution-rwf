#include "../ThreadActor.h"

#include "../Thread.h"

#include "Util/APITable.h"

namespace Threading {
    void ThreadActor::changeNodeAPIPre() {
        if (graphActor) {
            for (GameAPI::GameFaction faction : graphActor->factions) {
                faction.remove(actor);
            }
        }
    }

    void ThreadActor::changeNodeAPIPost() {
        for (GameAPI::GameFaction faction : graphActor->factions) {
            faction.add(actor);
        }

        for (GameAPI::GameFaction faction : graphActor->statFactions) {
            incrementStatFaction(faction);
        }


        if (!thread->isPlayerThread()) {
            return;
        }

        int playerIndex = thread->getPlayerIndex();

        for (Graph::Action::Action& action : thread->getCurrentNodeInternal()->actions) {
            if (action.roles.actor == playerIndex && action.roles.target == index) {
                for (GameAPI::GameFaction faction : action.attributes->roles.target.playerStatFactions) {
                    incrementStatFaction(faction);
                }
                for (GameAPI::GameList list : action.attributes->roles.target.playerStatLists) {
                    addToStatList(list);
                }
            } else if (action.roles.actor == index && action.roles.target == playerIndex) {
                for (GameAPI::GameFaction faction : action.attributes->roles.actor.playerStatFactions) {
                    incrementStatFaction(faction);
                }
                for (GameAPI::GameList list : action.attributes->roles.actor.playerStatLists) {
                    addToStatList(list);
                }
            }
        }
    }

    void ThreadActor::freeAPI() {
        if (this->graphActor) {
            for (GameAPI::GameFaction faction : this->graphActor->factions) {
                faction.remove(actor);
            }
        }
    }

    void ThreadActor::climaxAPI() {
        Util::APITable::getTimesClimaxedFaction().setRank(actor, timesClimaxed);

        for (GameAPI::GameFaction faction : graphActor->climaxStatFactions) {
            incrementFaction(faction);
        }

        for (Graph::Action::Action& action : thread->getCurrentNodeInternal()->actions) {
            if (action.roles.actor == index) {
                ThreadActor* partner = thread->GetActor(action.roles.target);
                for (GameAPI::GameFaction faction : action.attributes->roles.target.partnerClimaxStatFactions) {
                    partner->incrementStatFaction(faction);
                }

                if (actor.isPlayer()) {
                    for (GameAPI::GameFaction faction : action.attributes->roles.target.playerPartnerClimaxStatFactions) {
                        partner->incrementStatFaction(faction);
                    }

                    for (GameAPI::GameList list : action.attributes->roles.target.playerPartnerClimaxStatLists) {
                        partner->addToStatList(list);
                    }
                } else if (partner->actor.isPlayer()) {
                    for (GameAPI::GameFaction faction : action.attributes->roles.actor.playerClimaxStatFactions) {
                        incrementStatFaction(faction);
                    }

                    for (GameAPI::GameList list : action.attributes->roles.actor.playerClimaxStatLists) {
                        addToStatList(list);
                    }
                }
            } else if (action.roles.target == index) {
                ThreadActor* partner = thread->GetActor(action.roles.actor);
                for (GameAPI::GameFaction faction : action.attributes->roles.actor.partnerClimaxStatFactions) {
                    partner->incrementStatFaction(faction);
                }

                if (actor.isPlayer()) {
                    for (GameAPI::GameFaction faction : action.attributes->roles.actor.playerPartnerClimaxStatFactions) {
                        partner->incrementStatFaction(faction);
                    }

                    for (GameAPI::GameList list : action.attributes->roles.actor.playerPartnerClimaxStatLists) {
                        partner->addToStatList(list);
                    }
                } else if (partner->actor.isPlayer()) {
                    for (GameAPI::GameFaction faction : action.attributes->roles.target.playerClimaxStatFactions) {
                        incrementStatFaction(faction);
                    }

                    for (GameAPI::GameList list : action.attributes->roles.target.playerClimaxStatLists) {
                        addToStatList(list);
                    }
                }
            }
        }
    }


    void ThreadActor::incrementStatFaction(GameAPI::GameFaction faction) {
        if (statFactions.contains(faction.getIdentifier())) {
            return;
        }

        statFactions.insert(faction.getIdentifier());

        incrementFaction(faction);
    }

    void ThreadActor::incrementFaction(GameAPI::GameFaction faction) {
        if (faction.contains(actor)) {
            int rank = faction.getRank(actor);
            if (rank <= 100) {
                faction.setRank(actor, rank + 1);
            }
        } else {
            faction.add(actor, 1);
        }
    }

    void ThreadActor::addToStatList(GameAPI::GameList list) {
        if (!list.contains(actor)) {
            list.add(actor);
        }
    }
}