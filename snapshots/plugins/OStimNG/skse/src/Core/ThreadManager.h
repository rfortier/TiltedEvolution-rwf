#pragma once

#include "ThreadStarter/ThreadStartParams.h"
#include "Singleton.h"
#include "Thread.h"

#include "GameAPI/GameActor.h"
#include "Serial/OldThread.h"
#include "Util/IDGenerator.h"
#include <functional>
#include <shared_mutex>

namespace Threading {    
    class ThreadManager final : public ISingleton<ThreadManager> {
    public:
        ThreadManager();
        int startThread(ThreadStartParams params);
        Thread* GetThread(ThreadId a_id);
        Thread* getPlayerThread();
        void queueThreadStop(ThreadId threadID);
        void UntrackAllThreads();
        bool AnySceneRunning();
        bool playerThreadRunning();
        inline int getThreadCount() { return m_threadMap.size(); }
        std::vector<int> getAllThreadIDs();


        Thread* findThread(GameAPI::GameActor actor);
        Thread* findThread(GameAPI::GameObject furniture);

        ThreadActor* findActor(GameAPI::GameActor actor);

        std::vector<Serialization::OldThread> serialize();

        // Asynchronously migrate a thread (stop old, start new with same state).
        // Returns false if validation fails immediately, true if migration was scheduled.
        // onComplete is called with the new thread ID on success, or -1 on failure.
        bool migrateThread(ThreadId oldThreadId, std::vector<GameAPI::GameActor> newActors,
                           std::function<void(int32_t)> onComplete = nullptr, int startDelayMs = 500);

    private:
        using ThreadMap = std::unordered_map<ThreadId, Thread*>;
        Util::IDGenerator idGenerator = Util::IDGenerator(1);
        std::shared_mutex m_threadMapMtx;
        ThreadMap m_threadMap;
        std::thread m_excitementThread;
        std::vector<ThreadId> threadStopQueue;

        void stopThreadNoLock(ThreadId threadID);
        int startThreadNoLock(ThreadStartParams params);

#pragma region events
    public:
        inline void registerThreadStartListener(std::function<void(Thread*)> listener) { threadStartListeners.push_back(listener); }
        inline void registerSpeedChangedListener(std::function<void(Thread*)> listener) { speedChangedListeners.push_back(listener); }
        inline void registerNodeChangedListener(std::function<void(Thread*)> listener) { nodeChangedListeners.push_back(listener); }
        inline void registerClimaxListener(std::function<void(Thread*, ThreadActor*)> listener) { climaxListeners.push_back(listener); }
        inline void registerThreadEndListener(std::function<void(Thread*)> listener) { threadEndListeners.push_back(listener); }

    private:
        std::vector<std::function<void(Thread*)>> threadStartListeners;
        std::vector<std::function<void(Thread*)>> speedChangedListeners;
        std::vector<std::function<void(Thread*)>> nodeChangedListeners;
        std::vector<std::function<void(Thread*, ThreadActor*)>> climaxListeners;
        std::vector<std::function<void(Thread*)>> threadEndListeners;
#pragma endregion
    };
}  // namespace OStim