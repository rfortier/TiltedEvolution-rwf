#pragma once

#include "ThreadStarter/ThreadStartParams.h"

#include "Threading/Thread/NodeHandler.h"

#include <shared_mutex>

#include "AutoModeStage.h"
#include "ThreadActor.h"

#include "Alignment/ActorAlignment.h"
#include "Alignment/ThreadKey.h"
#include "GameAPI/GameActor.h"
#include "Graph/Node.h"
#include "PluginInterface/Threading/Thread.h"
#include "Serial/OldThread.h"
#include "Threading/Metadata.h"
#include "Util/VectorUtil.h"

namespace Threading {
    using ThreadId = int64_t;

    // State captured during thread migration to preserve context
    struct ThreadMigrationState {
        int currentSpeed = 0;
        Graph::Node* currentNode = nullptr;
        std::map<RE::FormID, float> actorExcitement;  // Key by actor FormID, not position!
        std::map<RE::FormID, int> actorTimesClimaxed;
        std::map<RE::FormID, int> actorTimeUntilClimax;
        std::map<RE::FormID, bool> actorUndressed;  // Track if actor was undressed
        std::vector<std::string> metadata;
        bool autoMode = false;
        AutoModeStage autoModeStage = AutoModeStage::NONE;
        int stopTimer = 0;
        ThreadFlags threadFlags = 0;
        GameAPI::GameObject furniture;
        Furniture::FurnitureType* furnitureType = nullptr;
        GameAPI::GameOwnership furnitureOwner = nullptr;
        // Camera state for player threads
        float freeCamSpeedBefore = 0;
        float worldFOVbefore = 0;
        float timeScaleBefore = 0;
    };

    class Thread : public OStim::Thread, public RE::BSTEventSink<RE::BSAnimationGraphEvent>{
    public:
        ThreadId m_threadId;

        Threading::Metadata metadata;

        Thread(int threadID, ThreadStartParams params);

        ~Thread();

        void initContinue(ThreadStartParams params);

        inline int getPlayerIndex() { return playerIndex; }

        inline ThreadFlags getThreadFlags() { return threadFlags; }
        inline bool isFlagged(ThreadFlag flag) { return (threadFlags & flag) == flag; }
        inline void flag(ThreadFlag flag) { threadFlags |= flag; }
        inline void unflag(ThreadFlag flag) { threadFlags &= ~flag; }

        inline Graph::Node* getCurrentNodeInternal() { return m_currentNode; }
        inline int getCurrentSpeed() { return m_currentNodeSpeed; }

        std::string getAlignmentKey();
        Alignment::ActorAlignment getActorAlignment(int index);
        void updateActorAlignment(int index, Alignment::ActorAlignment alignment);
        void alignActors();

        inline void setStopTimer(int timer) { stopTimer = timer; }
        void Navigate(std::string sceneId);

        void ChangeNode(Graph::Node* a_node);

        void AddActor(RE::Actor* a_actor);
        void RemoveActor();

        std::vector<Trait::ActorCondition> getActorConditions();

        void loop();

        inline std::map<int32_t, ThreadActor>& getActors() { return m_actors; }

        ThreadActor* GetActor(GameAPI::GameActor a_actor);
        ThreadActor* GetActor(int a_position);
        int getActorPosition(GameAPI::GameActor actor);
        std::vector<GameAPI::GameActor> getGameActors();

        void SetSpeed(int speed);
        bool increaseSpeed();
        bool decreaseSpeed();
        inline int getSpeed() { return m_currentNodeSpeed; }
        inline float getRelativeSpeed() { return m_currentNode ? static_cast<float>(m_currentNodeSpeed) / static_cast<float>(m_currentNode->speeds.size()) : 0; }

        float getMaxExcitement();

        void callEvent(std::string eventName, int actorIndex, int targetIndex, int performerIndex);

        void stop();
        void stopFaded();
        void close();
        
        // Thread migration for player add/remove
        void setMigrating(bool migrating) { isMigrating = migrating; }
        void setContinuingActors(const std::vector<GameAPI::GameActor>& actors);
        void restorePlayerSettings();
        void setThreadEndOriginator(const std::string& originator) { threadEndOriginator = originator; }
        ThreadMigrationState captureStateForMigration();
        void restoreStateFromMigration(const ThreadMigrationState& state);

        inline GameAPI::GamePosition getCenter() { return center; }

        inline Threading::Threads::NodeHandler* getNodeHandler() { return nodeHandler; }
    
        virtual RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource) override;

        Serialization::OldThread serialize();

    private:
        ThreadFlags threadFlags = 0;

        Graph::Node* m_currentNode = nullptr;
        bool playerThread = false;
        int playerIndex = -1;
        std::map<int32_t, ThreadActor> m_actors;

        std::shared_mutex nodeLock;

        GameAPI::GamePosition center;

        int m_currentNodeSpeed = 0;
        float relativeSpeed = -1;

        std::string alignmentKey;

        float freeCamSpeedBefore = 0;
        float worldFOVbefore = 0;
        float timeScaleBefore = 0;

        int stopTimer = 0;
        bool isStopping = false;
        bool isMigrating = false;  // Set during migration to skip camera/fade operations
        std::set<RE::FormID> continuingActors;  // Actors that will continue in new thread (use freeFast)
        std::string threadEndOriginator;  // What caused the thread to end (e.g., "add", "remove", "swap", "normal")

        Threading::Threads::NodeHandler* nodeHandler = nullptr;

        std::vector<Sound::SoundPlayer*> soundPlayers;

        void addActorInner(int index, RE::Actor* actor);
        void addActorSink(RE::Actor* a_actor);
        void removeActorSink(RE::Actor* a_actor);

        void rebuildAlignmentKey();
        void alignActor(ThreadActor* threadActor, Alignment::ActorAlignment alignment);

#pragma region autocontrol
    public:
        inline bool isInAutoMode() { return autoMode; }
        void startAutoMode();
        inline void stopAutoMode() { autoMode = false; }

        void setAutoModeToMainStage();

    private:
        int autoSpeedControlCooldown = 5000;

        bool autoMode = false;
        AutoModeStage autoModeStage = AutoModeStage::NONE;
        int foreplayThreshold = 0;
        int pulloutThreshold = 0;
        int autoModeCooldown = 0;

        void evaluateAutoMode();
        void startAutoModeCooldown();
        void progressAutoMode();
        void loopAutoControl();
        void nodeChangedAutoControl();
#pragma endregion

#pragma region climax
    public:
        inline bool getStallClimax() { return stallClimax; }
        inline void setStallClimax(bool stallClimax) { this->stallClimax = stallClimax; }
        bool isAnyActorAwaitingClimax();

    private:
        bool stallClimax = false;
#pragma endregion

#pragma region events
    public:
        // if you want to register for an event only for the duration of a node register to the NodeHandlers version of the event
        // NodeHandlers will be cleared on every node change
        inline void registerLoopListener(std::function<void()> listener) { loopListeners.push_back(listener); }
        inline void registerPeakListener(std::function<void(actionIndex)> listener) { peakListeners.push_back(listener); }
        inline void registerNodeChangedListener(std::function<void()> listener) { nodeChangedListeners.push_back(listener); }
        inline void registerSpeedChangedListener(std::function<void()> listener) { speedChangedListeners.push_back(listener); }
        inline void registerClimaxListener(std::function<void(ThreadActor*)> listener) { climaxListeners.push_back(listener); }
        inline void registerThreadEndListener(std::function<void()> listener) { threadEndListeners.push_back(listener); }

        void sendPeak(actionIndex peak);

    private:
        std::vector<std::function<void()>> loopListeners;
        std::vector<std::function<void(actionIndex)>> peakListeners;
        std::vector<std::function<void()>> nodeChangedListeners;
        std::vector<std::function<void()>> speedChangedListeners;
        std::vector<std::function<void(ThreadActor*)>> climaxListeners;
        std::vector<std::function<void()>> threadEndListeners;
#pragma endregion

#pragma region furniture
    public:
        inline GameAPI::GameObject getFurniture() { return furniture; }
        inline Furniture::FurnitureType* getFurnitureTypeInternal() { return furnitureType; }

        void changeFurniture(GameAPI::GameObject furniture, Graph::Node* node);

    private:
        GameAPI::GameObject furniture;
        Furniture::FurnitureType* furnitureType = nullptr;
        GameAPI::GameOwnership furnitureOwner = nullptr;
        float furnitureScaleMult = 1.0f;

        void changeFurnitureInner(GameAPI::GameObject furniture, Graph::Node* node);
        void addActorsToFurnitureFactions();
        void removeActorsFromFurnitureFactions();
#pragma endregion

#pragma region navigation
    public:
        void loopNavigation();
        bool autoTransition(std::string type);
        bool autoTransition(int index, std::string type);
        void warpTo(Graph::Node* node, bool useFades);
        void queueWarp(Graph::Node* node, int duration);
        inline void navigateTo(Graph::Node* node) { navigateTo(node, 0); }
        void navigateTo(Graph::Node* node, int duration);
        void queueNavigation(Graph::Node* node, int duration);
        void playSequence(Graph::Sequence* sequence, bool navigateTo, bool useFades);
        void playSequence(std::vector<Graph::SequenceEntry> nodes, bool navigateTo, bool useFades);
        bool pullOut();
        inline bool areNodesQueued() { return !nodeQueue.empty(); }
        inline bool isInSequence() { return inSequence; }

    private:
        bool inSequence = false;
        bool endAfterSequence = false;
        int nodeQueueCooldown = 0;
        std::queue<Graph::SequenceEntry> nodeQueue;

        void fadeAndChangeNode(Graph::Node* node);
        void clearNodeQueue();
#pragma endregion

#pragma region sound
    public:
        bool isAnyActorTalking();

    private:
        bool playingClimaxSound = false;
#pragma endregion


#pragma region abi
    public:
        virtual int32_t getThreadID() override;

        virtual bool isPlayerThread() override;

        virtual uint32_t getActorCount() override;
        virtual OStim::ThreadActor* getActor(uint32_t position) override;
        virtual void forEachThreadActor(OStim::ThreadActorVisitor* visitor) override;

        virtual OStim::Node* getCurrentNode() override;

        virtual OStim::FurnitureType* getFurnitureType() override;
        virtual void* getFurnitureObject() override;
#pragma endregion
    };

}  // namespace OStim