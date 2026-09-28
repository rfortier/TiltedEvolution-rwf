#pragma once

#include "Action/Action.h"
#include "GraphActor.h"
#include "NodeTag.h"
#include "RawNavigation.h"
#include "SequenceEntry.h"

#include "Furniture/Furniture.h"
#include "PluginInterface/Graph/Node.h"

namespace Threading {
    struct Thread;
}

namespace Graph {
    struct Node;

    struct Speed {
    public:
        std::string animation = "";
        float playbackSpeed = 1.0;
        float displaySpeed = -1;
    };

    struct Navigation {
        std::vector<Node*> nodes;
        std::string description = "";
        std::string icon = "";
        std::string border = "ffffff";
        bool isTransition = false;

        bool fulfilledBy(std::vector<Trait::ActorCondition> conditions);
        std::string getDescription(Threading::Thread* thread);
    };

    struct Node : public OStim::Node {
    public:
        std::string scene_id;
        std::string lowercase_id;
        uint32_t numActors;
        std::string scene_name;
        std::string lowercase_name;
        std::vector<Speed> speeds;
        uint32_t defaultSpeed = 0;
        bool isTransition = false;
        int animationLengthMs = 0;
        bool hasIdleSpeed = false;
        bool noRandomSelection = false;
        bool fadeOnEntry = false;
        Furniture::FurnitureType* furnitureType = nullptr;
        std::unordered_map<std::string, std::string> autoTransitions;
        std::vector<NodeTag> tags;
        std::vector<GraphActor> actors;
        std::vector<Action::Action> actions;
        std::vector<Navigation> navigations;
        std::string modpack = "";
        GameAPI::GamePosition offset;
        bool scaleOffsetWithFurniture = false;

        void tryAddTag(std::string tag);
        void mergeNodeIntoActors();

        bool hasTag(std::string tag);
        bool hasAnyTag(std::vector<std::string> tags);
        bool hasAllTags(std::vector<std::string> tags);
        bool hasOnlyTags(std::vector<std::string> tags);

        bool fulfilledBy(std::vector<Trait::ActorCondition> conditions);
        bool hasSameActorTpyes(Node* other);

        int getPrimaryPartner(int position);

        std::vector<Trait::FacialExpression*>* getFacialExpressions(int position);
        std::vector<Trait::FacialExpression*>* getOverrideExpressions(int position);

        uint32_t getStrippingMask(int position);
        bool doFullStrip(int position);

        std::string getAutoTransitionForNode(std::string type);
        std::string getAutoTransitionForActor(int position, std::string type);

        bool hasActorTag(int position, std::string tag);
        bool hasAnyActorTag(int position, std::vector<std::string> tags);
        bool hasAllActorTags(int position, std::vector<std::string> tags);
        bool hasOnlyListedActorTags(int position, std::vector<std::string> tags);
        bool hasActorTagOnAny(std::string tag);

        int findAction(std::function<bool(Action::Action)> condition);
        std::vector<int> findActions(std::function<bool(Action::Action)> condition);

        bool hasActionTag(std::string tag);

        int findAction(std::string type);
        int findAnyAction(std::vector<std::string> types);
        int findActionForActor(int position, std::string type);
        int findAnyActionForActor(int position, std::vector<std::string> types);
        int findActionForTarget(int position, std::string type);
        int findAnyActionForTarget(int position, std::vector<std::string> types);
        int findActionForActorAndTarget(int actorPosition, int targetPosition, std::string type);
        int findAnyActionForActorAndTarget(int actorPosition, int targetPosition, std::vector<std::string> types);

#pragma region navigation
    public:
        std::vector<Node*> getNodesInRange(int distance, std::vector<Trait::ActorCondition> actorConditions, std::function<bool(Node*)> nodeCondition);
        Node* getRandomNodeInRange(int distance, std::vector<Trait::ActorCondition> actorConditions, std::function<bool(Node*)> nodeCondition);
        std::vector<SequenceEntry> getRoute(int distance, std::vector<Trait::ActorCondition> actorConditions, Node* destination);
#pragma endregion

#pragma region abi
        virtual const char* getNodeID() override;

        virtual uint32_t getActorCount() override;
        virtual OStim::NodeActor* getActor(uint32_t index) override;
        virtual void forEachActor(OStim::NodeActorVisitor* visitor) override;

        virtual bool hasTag(const char* tag) override;
        virtual uint32_t getTagCount() override;
        virtual OStim::NodeTag* getTag(uint32_t index) override;
        virtual void forEachTag(OStim::NodeTagVisitor* visitor) override;

        virtual bool hasAction(const char* action) override;
        virtual uint32_t getActionCount() override;
        virtual OStim::Action* getAction(uint32_t index) override;
        virtual void forEachAction(OStim::ActionVisitor* visitor) override;

        virtual bool isValid(uint32_t actorCount, void** actors) override;

        virtual OStim::FurnitureType* getFurnitureType() override;
#pragma endregion
    };
}