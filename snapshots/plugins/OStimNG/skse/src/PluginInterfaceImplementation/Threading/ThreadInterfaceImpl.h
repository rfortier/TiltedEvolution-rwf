#pragma once

#include "PluginInterface/Threading/ThreadInterface.h"
#include "Util/Singleton.h"

namespace Interface {
    class ThreadInterfaceImpl : public OStim::ThreadInterface, public Util::Singleton<ThreadInterfaceImpl> {
    public:
        virtual uint32_t getVersion() override;

        virtual OStim::Thread* getThread(int32_t threadID) override;

        virtual void registerThreadStartListener(OStim::ThreadEventListener* listener) override;
        virtual void registerSpeedChangedListener(OStim::ThreadEventListener* listener) override;
        virtual void registerNodeChangedListener(OStim::ThreadEventListener* listener) override;
        virtual void registerClimaxListener(OStim::ThreadActorEventListener* listener) override;
        virtual void registerThreadStopListener(OStim::ThreadEventListener* listener) override;


        virtual OStim::ThreadBuilder* createThreadBuilder(uint32_t actorCount, void** actors) override;


        virtual OStim::ThreadActor* getActor(void* gameActor) override;
    };
}