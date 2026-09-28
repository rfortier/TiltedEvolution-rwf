#pragma once

#include "GraphActorTag.h"

#include "Action/Action.h"

#include "GameAPI/GamePosition.h"
#include "GameAPI/GameSex.h"
#include "PluginInterface/Graph/NodeActor.h"
#include "Trait/Condition.h"
#include "Trait/FacialExpression.h"

namespace Graph {
    struct GraphActor : public OStim::NodeActor {
    public:
        int sosBend = 0;
        float scale = 1.0;
        float scaleHeight = 120.748;
        bool feetOnGround = false;
        int expressionAction = -1;
        int animationIndex = -1;
        bool singleSpeed = false;
        std::string underlyingExpression = "";
        std::string expressionOverride = "";
        bool noStrip = false;
        bool moan = false;
        bool talk = false;
        bool muffled = false;
        GameAPI::GamePosition offset;
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
        std::unordered_map<int, Trait::FaceModifier> eyeballModifierOverride;
        std::vector<GraphActorTag> tags;
        Trait::ActorCondition condition;
        std::unordered_map<std::string, std::string> autoTransitions;

        void merge(Action::ActionActor& action);

        bool hasTag(std::string tag);
        bool hasAnyTag(std::vector<std::string> tags);
        bool hasAllTags(std::vector<std::string> tags);
        bool hasOnlyTags(std::vector<std::string> tags);

        virtual bool hasTag(const char* tag) override;
        virtual uint32_t getTagCount() override;
        virtual OStim::NodeActorTag* getTag(uint32_t index) override;
        virtual void forEachTag(OStim::NodeActorTagVisitor* visitor) override;
    };
}