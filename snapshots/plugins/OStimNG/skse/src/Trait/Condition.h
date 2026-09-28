#pragma once

#include "GameAPI/GameActor.h"
#include "GameAPI/GameSex.h"

namespace Threading {
    class ThreadActor;
}

namespace Trait {
    struct ActorCondition {
    public:
        static ActorCondition create(GameAPI::GameActor actor);
        static ActorCondition create(Threading::ThreadActor* actor);
        static std::vector<ActorCondition> create(std::vector<GameAPI::GameActor> actors);

        std::string type = "npc";
        GameAPI::GameSex sex = GameAPI::GameSex::AGENDER;
        std::set<std::string> requirements;
        
        bool fulfills(std::set<std::string>& requirements);
        bool fulfills(ActorCondition conditions);

    private:
        static ActorCondition create(GameAPI::GameActor actor, Threading::ThreadActor* threadActor);
    };
}