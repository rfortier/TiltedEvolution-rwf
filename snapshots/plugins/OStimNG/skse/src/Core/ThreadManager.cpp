#include "Core/ThreadManager.h"
#include "UI/UIState.h"
#include "ModAPI/OstimNG-API-Thread.h"

#include "Furniture/FurnitureTable.h"
#include "GameAPI/Game.h"
#include "GameAPI/GameCamera.h"
#include "Graph/GraphTable.h"
#include "Trait/Condition.h"
#include "Util/Constants.h"
#include "Util/EventUtil.h"
#include "Util/VectorUtil.h"

namespace Threading {
    ThreadManager::ThreadManager() {
        // Register Thread API listeners
        registerThreadStartListener([](Thread* thread) {
            if (thread->isPlayerThread()) {
                OstimNG_API::Thread::NotifyEvent(
                    OstimNG_API::Thread::ThreadEvent::ThreadStarted, thread->m_threadId);
            }
        });

        registerNodeChangedListener([](Thread* thread) {
            if (thread->isPlayerThread()) {
                OstimNG_API::Thread::NotifyEvent(
                    OstimNG_API::Thread::ThreadEvent::NodeChanged, thread->m_threadId);
            }
        });

        registerThreadEndListener([](Thread* thread) {
            if (thread->isPlayerThread()) {
                OstimNG_API::Thread::NotifyEvent(
                    OstimNG_API::Thread::ThreadEvent::ThreadEnded, thread->m_threadId);
            }
        });

        m_excitementThread = std::thread([&]() {
            auto sleepTime = std::chrono::milliseconds(Constants::LOOP_TIME_MILLISECONDS);

            while (true) {
                auto start = std::chrono::system_clock::now();

                if (!GameAPI::Game::isPaused()) {
                    std::shared_lock<std::shared_mutex> lock(m_threadMapMtx);
                    for (auto& it : m_threadMap) {
                        it.second->loop();
                    }

                    for (ThreadId& threadID : threadStopQueue) {
                        stopThreadNoLock(threadID);
                    }
                    threadStopQueue.clear();

                    UI::UIState::GetSingleton()->loop();
                }

                auto end = std::chrono::system_clock::now();
                auto time = end - start;
                if (time < sleepTime) {
                    std::this_thread::sleep_for(sleepTime - time);
                }

                //std::this_thread::sleep_for(std::chrono::milliseconds(Constants::LOOP_TIME_MILLISECONDS));
            }
        });
        m_excitementThread.detach();
    }


    int ThreadManager::startThread(ThreadStartParams params) {
        std::unique_lock<std::shared_mutex> lock(m_threadMapMtx);
        return startThreadNoLock(params);
    }

    int ThreadManager::startThreadNoLock(ThreadStartParams params) {
        int threadID = -1;
        for (GameAPI::GameActor actor : params.actors) {
            if (actor.isPlayer()) {
                threadID = 0;
                break;
            }
        }
        if (threadID == 0) {
            if (m_threadMap.contains(0)) {
                GameAPI::Game::notification("main thread already running");
                return -1;
            }
        } else {
            threadID = idGenerator.get();
        }

        Thread* thread = new Thread(threadID, params);
        m_threadMap.insert(std::make_pair(threadID, thread));

        thread->registerSpeedChangedListener([this, thread]() { EventUtil::invokeListeners(speedChangedListeners, thread); });
        thread->registerNodeChangedListener([this, thread]() { EventUtil::invokeListeners(nodeChangedListeners, thread); });
        thread->registerClimaxListener([this, thread](ThreadActor* actor) { EventUtil::invokeListeners(climaxListeners, thread, actor); });
        thread->registerThreadEndListener([this, thread]() { EventUtil::invokeListeners(threadEndListeners, thread); });

        thread->initContinue(params);
        
        EventUtil::invokeListeners(threadStartListeners, thread);

        return threadID;
    }

    Thread* ThreadManager::GetThread(ThreadId a_id) {
        std::shared_lock<std::shared_mutex> lock(m_threadMapMtx);
        auto it = m_threadMap.find(a_id);
        if (it == m_threadMap.end()) {
            return nullptr;
        }
        return it->second;
    }

    Thread* ThreadManager::getPlayerThread() {
        std::shared_lock<std::shared_mutex> lock(m_threadMapMtx);
        for (auto& [threadID, thread] : m_threadMap) {
            if (thread->isPlayerThread()) {
                return thread;
            }
        }
        return nullptr;
    }

    void ThreadManager::queueThreadStop(ThreadId threadID) {
        if (!VectorUtil::contains(threadStopQueue, threadID)) {
            threadStopQueue.push_back(threadID);
        }
    }

    void ThreadManager::UntrackAllThreads() {
        // this is a force close due to the user loading another save
        // so no need to free actors etc. here
        std::unique_lock<std::shared_mutex> lock(m_threadMapMtx);
        for (auto& entry : m_threadMap) {
            delete entry.second;
        }
        m_threadMap.clear();
        idGenerator.reset();
    }

    bool ThreadManager::AnySceneRunning() {
        return m_threadMap.size() > 0;
    }

    bool ThreadManager::playerThreadRunning() {
        std::shared_lock<std::shared_mutex> lock(m_threadMapMtx);
        for (auto& [id, thread] : m_threadMap) {
            if (thread->isPlayerThread()) {
                return true;
            }
        }
        return false;
    }

    Thread* ThreadManager::findThread(GameAPI::GameActor actor) {
        std::shared_lock<std::shared_mutex> lock(m_threadMapMtx);
        for (auto& [id, thread] : m_threadMap) {
            ThreadActor* threadActor = thread->GetActor(actor);
            if (threadActor) {
                return thread;
            }
        }

        return nullptr;
    }

    Thread* ThreadManager::findThread(GameAPI::GameObject furniture) {
        std::shared_lock<std::shared_mutex> lock(m_threadMapMtx);
        for (auto& [id, thread] : m_threadMap) {
            if (thread->getFurniture() == furniture) {
                return thread;
            }
        }

        return nullptr;
    }

    ThreadActor* ThreadManager::findActor(GameAPI::GameActor actor) {
        std::shared_lock<std::shared_mutex> lock(m_threadMapMtx);
        for (auto&[id, thread] : m_threadMap) {
            ThreadActor* threadActor = thread->GetActor(actor);
            if (threadActor) {
                return threadActor;
            }
        }

        return nullptr;
    }

    // serialized the currently running threads so that the actors can be properly set free on game load
    std::vector<Serialization::OldThread> ThreadManager::serialize() {
        std::vector<Serialization::OldThread> oldThreads;

        for (auto& it : m_threadMap) {
            oldThreads.push_back(it.second->serialize());
        }

        return oldThreads;
    }

    void ThreadManager::stopThreadNoLock(ThreadId threadID) {
        logger::info("trying to stop thread {}", threadID);
        auto it = m_threadMap.find(threadID);
        if (it != m_threadMap.end()) {
            Thread* thread = it->second;
            UI::UIState::GetSingleton()->HandleThreadRemoved(thread);
            m_threadMap.erase(threadID);
            thread->close();
            delete thread;
            if (threadID != 0) {
                idGenerator.free(threadID);
            }
        } else {
            logger::info("no thread found with id {}", threadID);
        }
    }
    

    std::vector<int> ThreadManager::getAllThreadIDs() {
        std::vector<int> ids;
        for (auto& [id, thread] : m_threadMap) {
            ids.push_back(id);
        }
        return ids;
    }

    bool ThreadManager::migrateThread(ThreadId oldThreadId, std::vector<GameAPI::GameActor> newActors,
                                       std::function<void(int32_t)> onComplete, int startDelayMs) {
        logger::info("migrating thread {} with {} actors - COMPLETE STOP THEN START", oldThreadId, newActors.size());
        
        ThreadMigrationState state;
        Graph::Node* selectedStartNode = nullptr;
        
        // Scope for lock - capture state and release before searching for node
        {
            std::unique_lock<std::shared_mutex> lock(m_threadMapMtx);

            // Get the old thread
            auto it = m_threadMap.find(oldThreadId);
            if (it == m_threadMap.end()) {
                logger::error("cannot migrate: thread {} not found", oldThreadId);
                if (onComplete) onComplete(-1);
                return false;
            }
            Thread* oldThread = it->second;

            // Capture state BEFORE stopping (while thread is still valid)
            state = oldThread->captureStateForMigration();
            
            // Determine the migration type for event logging
            std::string originator;
            size_t oldActorCount = oldThread->getActors().size();
            size_t newActorCount = newActors.size();
            if (newActorCount > oldActorCount) {
                originator = "add";
            } else if (newActorCount < oldActorCount) {
                originator = "remove";
            } else {
                originator = "swap";
            }
            oldThread->setThreadEndOriginator(originator);

            // Sort new actors in proper order only when actor count changes (add/remove)
            if (newActorCount != oldActorCount) {
                std::vector<GameAPI::GameActor> dominantActors;
                ActorUtil::sort(newActors, dominantActors, -1);
            }
        }
        // Lock released - can now safely call ActorCondition::create and getRandomNode
        
        // BEFORE stopping old thread, verify we can find a starting node for new actor count
        if (state.currentNode && state.currentNode->actors.size() == newActors.size()) {
            // Actor count unchanged - can restore same node and speed
            logger::info("Actor count unchanged ({}), will restore node {} with speed {}",
                newActors.size(), state.currentNode->scene_id, state.currentSpeed);
            selectedStartNode = state.currentNode;
        } else {
            // Actor count changed - must find appropriate starting node for new count
            logger::info("Actor count changed from {} to {}, finding appropriate starting node",
                state.currentNode ? state.currentNode->actors.size() : 0, newActors.size());
            
            // When actor count changes, always use "none" furniture type
            // (the old furniture might not have scenes for the new actor count)
            Furniture::FurnitureType* searchFurnitureType = Furniture::FurnitureTable::getFurnitureType("none");
            
            logger::info("Searching with 'none' furniture type (actor count changed)");
            
            // First check if ANY nodes exist for this actor count
            if (Graph::GraphTable::hasNodes(searchFurnitureType, newActors.size())) {
                logger::info("Nodes exist for {} actors with 'none' furniture", newActors.size());
            } else {
                logger::warn("NO nodes found for {} actors with 'none' furniture!", newActors.size());
            }
            
            // Try WITHOUT the transition filter first
            selectedStartNode = Graph::GraphTable::getRandomNode(
                searchFurnitureType, 
                Trait::ActorCondition::create(newActors),
                nullptr  // No filter - accept ANY node including transitions
            );
            
            logger::info("getRandomNode (no filter) returned: {}", selectedStartNode ? selectedStartNode->scene_id.c_str() : "NULL");
            
            if (!selectedStartNode) {
                logger::error("CANNOT MIGRATE: No suitable starting node found for {} actors - ABORTING migration", newActors.size());
                if (onComplete) onComplete(-1);
                return false;
            }
            
            logger::info("Selected starting node {} for {} actors", selectedStartNode->scene_id, newActors.size());
        }
        
        // Re-acquire lock to stop old thread and start new one
        std::unique_lock<std::shared_mutex> lock(m_threadMapMtx);
        
        // Verify thread still exists (could have been stopped while we were searching)
        auto it = m_threadMap.find(oldThreadId);
        if (it == m_threadMap.end()) {
            logger::error("thread {} was stopped during node search - ABORTING migration", oldThreadId);
            if (onComplete) onComplete(-1);
            return false;
        }
        Thread* oldThread = it->second;
        
        // Check if player is being removed
        bool oldHadPlayer = oldThread->isPlayerThread();
        bool newWillHavePlayer = false;
        for (auto& actor : newActors) {
            if (actor.isPlayer()) {
                newWillHavePlayer = true;
                break;
            }
        }
        
        // If player is being removed, restore settings before stopping thread
        if (oldHadPlayer && !newWillHavePlayer) {
            logger::info("Player being removed - restoring camera/UI settings");
            oldThread->restorePlayerSettings();
        }
        
        // Tell thread which actors are continuing (will use freeFast), others will use free()
        oldThread->setContinuingActors(newActors);
        
        // Now that we've confirmed we have a valid starting node, proceed with migration
        oldThread->setMigrating(true);
        
        // Remove from UI and map under lock; close()/delete deferred to main thread below.
        UI::UIState::GetSingleton()->HandleThreadRemoved(oldThread);
        m_threadMap.erase(oldThreadId);

        // close() mutates the NiNode tree (via actor.unequip/facelight detach), which races
        // with hdtSMP64's background ShadowSceneNode traversal. Must run on the main thread.
        Thread* closingThread = oldThread;
        bool shouldFreeId = (oldThreadId != 0);
        ThreadId closingId = oldThreadId;

        // Release lock before async operation
        lock.unlock();

        // Create new thread start params with the selected starting node
        ThreadStartParams params;
        params.actors = newActors;
        params.furniture = state.furniture;
        params.threadFlags = state.threadFlags;
        params.metadata = state.metadata;

        Graph::SequenceEntry entry;
        entry.node = selectedStartNode;
        params.startingNodes.push_back(entry);

        // Static fade durations (seconds / milliseconds)
        constexpr float FADE_OUT_DURATION = 0.5f;
        constexpr int   FADE_OUT_MS       = 500;
        constexpr float FADE_IN_DURATION  = 1.0f;

        // Begin fade to black. The background thread sleeps for FADE_OUT_MS so that
        // the old thread is only stopped once the screen has fully gone black.
        GameAPI::GameCamera::fadeToBlack(FADE_OUT_DURATION);

        // Step 1: wait for screen to go black (FADE_OUT_MS).
        // Step 2: close old thread on main game thread (NiNode-safe).
        // Step 3: wait configured timeout (startDelayMs) for cleanup tasks to finish.
        // Step 4: start new thread on main game thread.
        // Step 5: fade back from black.
        std::thread([this, params, state, closingThread, closingId, shouldFreeId, onComplete, startDelayMs, FADE_IN_DURATION]() {
            // Wait until the screen is fully black before stopping the old thread
            std::this_thread::sleep_for(std::chrono::milliseconds(FADE_OUT_MS));

            GameAPI::Game::runSynced([closingThread, closingId, shouldFreeId, this]() {
                closingThread->close();
                delete closingThread;
                if (shouldFreeId) {
                    idGenerator.free(closingId);
                }
                logger::info("Old thread {} FULLY STOPPED on main thread - all cleanup complete", closingId);
            });

            logger::info("Waiting {} ms for old thread cleanup tasks to finish...", startDelayMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(startDelayMs));

            logger::info("STARTING new thread with {} actors after delay", params.actors.size());
            GameAPI::Game::runSynced([this, params, state, onComplete, closingId]() {
                int newThreadId = startThread(params);  // acquires lock internally

                if (newThreadId < 0) {
                    logger::error("failed to start new thread after migration");
                    if (onComplete) onComplete(-1);
                    return;
                }

                Thread* newThread = m_threadMap[newThreadId];
                if (newThread) {
                    newThread->restoreStateFromMigration(state);
                    logger::info("successfully migrated thread {} to thread {}", closingId, newThreadId);
                    if (onComplete) onComplete(newThreadId);
                } else {
                    logger::error("failed to get new thread after migration");
                    if (onComplete) onComplete(-1);
                }
            });

            // New thread is running - fade back from black
            GameAPI::GameCamera::fadeFromBlack(FADE_IN_DURATION);
        }).detach();

        logger::info("Migration scheduled - returning immediately");
        return true;  // migration scheduled; onComplete delivers the real thread ID
    }
}  // namespace OStim