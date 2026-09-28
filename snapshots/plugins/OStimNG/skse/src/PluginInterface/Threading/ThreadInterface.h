#pragma once

#include "Thread.h"
#include "ThreadActorEventListener.h"
#include "ThreadBuilder.h"
#include "ThreadEventListener.h"

#include "../PluginInterface.h"

namespace OStim {
    class ThreadInterface : public PluginInterface {
    public:
        inline static const char* NAME = "Threads";

        // --- ABI version 1 ---

        /// <summary>
        /// gets the thread with the given ID
        /// returns nullptr if no thread with the ID is running
        /// </summary>
        /// <param name="threadID">the ID of the thread to find</param>
        /// <returns>the thread, or a nullptr if no thread with that ID is running</returns>
        virtual Thread* getThread(int32_t threadID) = 0;

        virtual void registerThreadStartListener(ThreadEventListener* listener) = 0;
        virtual void registerSpeedChangedListener(ThreadEventListener* listener) = 0;
        virtual void registerNodeChangedListener(ThreadEventListener* listener) = 0;
        virtual void registerClimaxListener(ThreadActorEventListener* listener) = 0;
        virtual void registerThreadStopListener(ThreadEventListener* listener) = 0;

        // --- ABI version 2 ---
        virtual ThreadBuilder* createThreadBuilder(uint32_t actorCount, void** actors) = 0;

        // --- ABI verison 3 ---
        virtual ThreadActor* getActor(void* gameActor) = 0;
    };
}