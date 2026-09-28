#pragma once

#include "Event.h"
#include "Node.h"
#include "Requirement.h"
#include "Sequence.h"

#include "Furniture/FurnitureType.h"
#include "Trait/Condition.h"
#include "Option.h"

namespace Graph{
    class GraphTable {
#pragma region actions
    public:
        static void SetupActions();
        static std::vector<std::string> getActions();
        static std::string getActionAlias(std::string type);
        static Action::ActionAttributes* getActionAttributesByType(std::string type);

        static Action::ActionTag* getActionTag(std::string id);
        static Action::ActionAttributes* getActionTypeUnsafe(std::string id);

    private:
        inline static std::unordered_map<std::string, Action::ActionTag> actionTags;
        inline static std::unordered_map<std::string, std::string> actionAliases;
        inline static std::unordered_map<std::string, Action::ActionAttributes> actions;
#pragma endregion

#pragma region events
    public:
        static void setupEvents();
        static std::vector<std::string> getEvents();
        static Event* getEvent(std::string eventName);

    private:
        inline static std::unordered_map<std::string, Event> events;
#pragma endregion

#pragma region nodes
    public:
        static void setupNodes();
        static void reloadNode(std::string sceneId);
        static void addNode(Node* node);
        static void addNavigations(std::vector<RawNavigation> navigations);
        static std::vector<Node*> getNodes();
        static Node* getNodeById(std::string id);
        static void searchNodesByName(std::string& name, std::vector<Node*>& results);
        static Node* getNodeByAnimation(std::string anim);

        static bool hasNodes(Furniture::FurnitureType* furnitureType, int actorCount);
        static Node* getRandomNode(Furniture::FurnitureType* furnitureType, std::vector<Trait::ActorCondition> actorConditions, std::function<bool(Node*)> nodeCondition);

    private:
        inline static std::unordered_map<std::string, Node*> nodes;
        inline static std::unordered_map<std::string, Node*> animationNodeTable;
        inline static std::unordered_map<Furniture::FurnitureType*, std::unordered_map<int, std::vector<Node*>*>*> nodeList;
#pragma endregion

#pragma region sequences
    public:
        static void setupSequences();
        static Sequence* getSequenceByID(std::string id);
        static Sequence* getRandomSequence(Furniture::FurnitureType* furnitureType, std::vector<Trait::ActorCondition> actorConditions, std::function<bool(Sequence*)> sequenceCondition);

    private:
        inline static std::unordered_map<std::string, Sequence> sequences;
#pragma endregion

#pragma region options
    public:
        static void setupOptions();
        static MenuNode& getRootOptionsNode() { return rootNode; };
        static GameAPI::GameFaction& getOptionFaction(std::string faction) {
            return optionFactions.find(faction)->second;
        }
        
    private:
        static MenuNode* constructHeirarchy(MenuNode* leaf, std::vector<MenuNode*>& heirarchy, std::unordered_map<std::string, MenuNode>& rawPages);
        static MenuNode* findParent(Graph::MenuNode* findingNode, std::vector<Graph::MenuNode>* options);
        inline static MenuNode rootNode;  
        inline static std::unordered_map<std::string, GameAPI::GameFaction> optionFactions;
#pragma endregion

    };
}