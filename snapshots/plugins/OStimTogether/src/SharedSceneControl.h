#pragma once

#include "PCH.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"

namespace OStimTogether
{
    class SharedSceneControl
    {
    public:
        static SharedSceneControl& GetSingleton();
        bool Initialize();

    private:
        SharedSceneControl() = default;

        class StartListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        void HandleStart(OStim::Thread* thread);
        void EnablePlayerControl(std::int32_t threadID);
        bool IsMultiplayerPlayerThread(OStim::Thread* thread) const;

        OStim::ThreadInterface* _threads{ nullptr };
        StartListener _startListener;
        std::atomic_bool _initialized{ false };
    };
}
