#pragma once

#include "PluginInterface.h"
#include "Thread.h"
#include "ThreadEventListener.h"
#include "ThreadBuilder.h"

namespace OStim
{
    class ThreadInterface : public PluginInterface
    {
    public:
        inline static const char* NAME = "Threads";

        virtual Thread* getThread(std::int32_t threadID) = 0;
        virtual void registerThreadStartListener(
            ThreadEventListener* listener) = 0;
        virtual void registerSpeedChangedListener(
            ThreadEventListener* listener) = 0;
        virtual void registerNodeChangedListener(
            ThreadEventListener* listener) = 0;
        virtual void registerClimaxListener(void* listener) = 0;
        virtual void registerThreadStopListener(
            ThreadEventListener* listener) = 0;

        virtual ThreadBuilder* createThreadBuilder(
            std::uint32_t actorCount,
            void** actors) = 0;

        virtual ThreadActor* getActor(void* gameActor) = 0;
    };
}
