#pragma once

#include "GameAPI/GameFaction.h"
#include "GameAPI/GameList.h"

namespace Util {
    class APITable {
    public:
        static void setupForms();

        static inline GameAPI::GameFaction getActorCountFaction() { return OStimActorCountFaction; }
        static inline GameAPI::GameFaction getExcitementFaction() { return OStimExcitementFaction; }
        static inline GameAPI::GameFaction getInWaterFaction() { return OStimInWaterFaction; }
        static inline GameAPI::GameFaction getSchlongifiedFaction() { return OStimSchlongifiedFaction; }
        static inline GameAPI::GameFaction getTimesClimaxedFaction() { return OStimTimesClimaxedFaction; }
        static inline GameAPI::GameFaction getTimeUntilClimaxFaction() { return OStimTimeUntilClimaxFaction; }

        static inline GameAPI::GameList getActorList() { return OStatActorList; }
        static inline GameAPI::GameList getPlayerPartnerList() { return OStatPlayerPartnerList; }

    private:
        static inline GameAPI::GameFaction OStimActorCountFaction;
        static inline GameAPI::GameFaction OStimExcitementFaction;
        static inline GameAPI::GameFaction OStimInWaterFaction;
        static inline GameAPI::GameFaction OStimSchlongifiedFaction;
        static inline GameAPI::GameFaction OStimTimesClimaxedFaction;
        static inline GameAPI::GameFaction OStimTimeUntilClimaxFaction;

        static inline GameAPI::GameList OStatActorList;
        static inline GameAPI::GameList OStatPlayerPartnerList;
    };
}