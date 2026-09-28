#include "GameFaction.h"

namespace GameAPI {
    void GameFaction::setRank(GameActor actor, int rank) const {
        for (auto& factionInfo : actor.form->GetActorBase()->factions) {
            if (factionInfo.faction == form) {
                factionInfo.rank = rank;
            }
        }
    }

    int GameFaction::getRank(GameActor actor) const {
        for (RE::FACTION_RANK rank : actor.form->GetActorBase()->factions) {
            if (rank.faction == form) {
                return rank.rank;
            }
        }

        return -2;
    }
}