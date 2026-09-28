#pragma once

#include "GameActor.h"
#include "GameObject.h"

#include "Graph/Event.h"
#include "Graph/RoleMap.h"

namespace Threading {
    class Thread;
}

namespace GameAPI {
    namespace GameEvents {
        void sendStartEvent(int threadID);
        void sendSceneChangedEvent(int threadID, std::string sceneID);
        void sendSpeedChangedEvent(int threadID, std::string sceneID, int speed);
        void sendEndEvent(int threadID, Threading::Thread* thread, std::vector<GameActor> actors, const std::string& originator = "");

        void sendOrgasmEvent(int threadID, std::string sceneID, int index, GameAPI::GameActor actor);
        void sendFurnitureChangedEvent(int threadID, GameAPI::GameObject furniture);
        void sendOStimEvent(int threadID, Graph::Event* graphEvent, Graph::RoleMap<GameActor> actors);
    }
}