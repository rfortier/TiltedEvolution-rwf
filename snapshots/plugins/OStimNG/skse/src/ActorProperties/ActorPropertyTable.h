#pragma once

#include "ActorPropertyList.h"

#include "GameAPI/GameActor.h"

namespace ActorProperties {
    class ActorPropertyTable {
    public:
        static void setup();

        inline static std::string getActorType(GameAPI::GameActor actor) { return actorTypes.get(actor, ""); }
        inline static GameAPI::GameSex getActorSex(GameAPI::GameActor actor) { return actorSexes.get(actor, GameAPI::GameSex::AGENDER); }
        inline static std::string getActorExpression(GameAPI::GameActor actor) {
            return actorExpressions.get(actor, "");
        }
        inline static bool isActorMuted(GameAPI::GameActor actor) { return actorMutes.get(actor, false); }
        inline static bool isActorMuffled(GameAPI::GameActor actor) { return actorMuffles.get(actor, false); }
        static std::set<std::string> getActorRequirements(GameAPI::GameActor actor);
        static std::set<std::string> getEquipObjects(GameAPI::GameActor actor);

    private:
        inline static ActorPropertyList<std::string> actorTypes;
        inline static ActorPropertyList<GameAPI::GameSex> actorSexes;
        inline static ActorPropertyList<std::string> actorExpressions;
        inline static ActorPropertyList<bool> actorMutes;
        inline static ActorPropertyList<bool> actorMuffles;
        inline static std::unordered_map<std::string, ActorPropertyList<bool>> actorRequirements;
        inline static std::unordered_map<std::string, ActorPropertyList<bool>> equipObjects;
    };
}