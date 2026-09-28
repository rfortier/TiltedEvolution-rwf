#pragma once

#include "GameAPI/GameActor.h"
#include "GameAPI/GameFaction.h"
#include "GameAPI/GameList.h"

namespace Compatibility {
    class CompatibilityTable {
    public:
        static void setupForms();

        inline static bool sosInstalled() { return SOS_SchlongifiedFaction; }
        inline static bool tngInstalled() { return TNG_Gentified; }
        inline static bool customGenitalInstalled() { return sosInstalled() || tngInstalled(); }
        static bool hasSchlong(GameAPI::GameActor actor);
    private:
        static void tryAddNoSchlongFaction(std::string modName, RE::FormID formID);

        inline static GameAPI::GameFaction SOS_SchlongifiedFaction;
        inline static std::vector<GameAPI::GameFaction> noSchlongFactions;

        inline static GameAPI::GameList TNG_Gentified;
        inline static GameAPI::GameList TNG_Ungentified;
    };
}