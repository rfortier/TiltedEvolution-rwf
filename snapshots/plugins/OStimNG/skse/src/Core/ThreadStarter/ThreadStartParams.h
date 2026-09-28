#pragma once

#include "Core/ThreadFlag.h"

#include "GameAPI/GameActor.h"
#include "GameAPI/GameObject.h"
#include "Graph/Node.h"
#include "Graph/Sequence.h"

namespace Threading {
    struct ThreadStartParams {
        std::vector<GameAPI::GameActor> actors;
        std::vector<GameAPI::GameActor> dominantActors;
        GameAPI::GameObject furniture;
        int duration = 0;
        std::vector<Graph::SequenceEntry> startingNodes;
        bool endAfterSequence = false;
        bool noFurniture = false;
        ThreadFlags threadFlags = 0;
        std::vector<std::string> metadata;

        void setStartingNode(std::string nodeID);
        void addStartingNode(std::string nodeID, int duration, bool navigateTo);
        void setStartingSequence(std::string sequenceID);
        void concatStartingSequence(std::string sequenceID, bool navigateTo);
    };
}