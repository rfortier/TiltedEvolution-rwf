#pragma once

#include "PCH.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"

namespace OStimTogether
{
    class PreflightGuard
    {
    public:
        static PreflightGuard& GetSingleton();
        bool Initialize();

    private:
        PreflightGuard() = default;

        class StartListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        void HandleStart(OStim::Thread* thread);

        OStim::ThreadInterface* _threads{ nullptr };
        StartListener _startListener;
        std::atomic_bool _initialized{ false };
    };
}
