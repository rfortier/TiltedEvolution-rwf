#pragma once

#include "GameAPI/GameFaction.h"
#include "GameAPI/GameList.h"
#include "PluginInterface/Graph/ActionActor.h"

namespace Graph {
    namespace Action {
        struct ActionActor : OStim::ActionActor {
        public:
            float stimulation = 0.0;
            float maxStimulation = 100.0;
            bool fullStrip = false;
            bool moan = false;
            bool talk = false;
            bool muffled = false;
            std::string expressionOverride = "";
            std::set<std::string> requirements;
            uint32_t strippingMask = 0;
            std::set<std::string> equipObjects;

            std::vector<GameAPI::GameFaction> factions;
            std::vector<GameAPI::GameFaction> statFactions;
            std::vector<GameAPI::GameFaction> playerStatFactions;
            std::vector<GameAPI::GameFaction> climaxStatFactions;
            std::vector<GameAPI::GameFaction> partnerClimaxStatFactions;
            std::vector<GameAPI::GameFaction> playerClimaxStatFactions;
            std::vector<GameAPI::GameFaction> playerPartnerClimaxStatFactions;
            std::vector<GameAPI::GameList> playerStatLists;
            std::vector<GameAPI::GameList> playerClimaxStatLists;
            std::vector<GameAPI::GameList> playerPartnerClimaxStatLists;

            GameAPI::GameFaction displayStatFaction;
            GameAPI::GameFaction displayPlayerStatFaction;
            GameAPI::GameFaction displayClimaxStatFaction;
            GameAPI::GameFaction displayPartnerClimaxStatFaction;
            GameAPI::GameFaction displayPlayerClimaxStatFaction;
            GameAPI::GameFaction displayPlayerPartnerClimaxStatFaction;
            GameAPI::GameList displayPlayerStatList;
            GameAPI::GameList displayPlayerClimaxStatList;
            GameAPI::GameList displayPlayerPartnerClimaxStatList;

            std::unordered_map<std::string, int> ints;
            std::unordered_map<std::string, std::vector<int>> intLists;
            std::unordered_map<std::string, float> floats;
            std::unordered_map<std::string, std::vector<float>> floatLists;
            std::unordered_map<std::string, std::string> strings;
            std::unordered_map<std::string, std::vector<std::string>> stringLists;
            std::vector<std::string> toySlots;

            void merge(ActionActor& other);

#pragma region ABI
            virtual void* getTimesPerformedFaction();
            virtual void* getTimesPerformedWithPlayerFaction();
            virtual void* getTimesClimaxedFaction();
            virtual void* getTimesPartnerClimaxedFaction();
            virtual void* getTimesClimaxedWithPlayerFaction();
            virtual void* getTimesPartnerClimaxedWithPlayerFaction();

            virtual void* getPlayerMateList();
            virtual void* getPlayerMateClimaxedList();
            virtual void* getPlayerMatePartnerClimaxedList();
#pragma endregion
        };
    }
}