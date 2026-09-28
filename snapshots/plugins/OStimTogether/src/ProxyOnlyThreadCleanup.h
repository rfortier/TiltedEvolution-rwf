#pragma once

#include "PCH.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"
#include "OStimAPI/ModSceneControl.h"

namespace OStimTogether
{
    class ProxyOnlyThreadCleanup
    {
    public:
        static ProxyOnlyThreadCleanup& GetSingleton();
        bool Initialize();

    private:
        ProxyOnlyThreadCleanup() = default;

        class StartListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        void HandleStart(OStim::Thread* thread);
        bool LoadSceneAPI();

        OStim::ThreadInterface* _threads{ nullptr };
        OStimModAPI::Scene::ISceneInterface* _sceneControl{ nullptr };
        StartListener _startListener;
        std::atomic_bool _initialized{ false };
    };
}
