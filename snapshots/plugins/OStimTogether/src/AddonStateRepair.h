#pragma once

#include "PCH.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"

namespace OStimTogether
{
    class AddonStateRepair
    {
    public:
        static AddonStateRepair& GetSingleton();
        bool Initialize();

    private:
        class NodeListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        AddonStateRepair() = default;
        void HandleNode(OStim::Thread* thread);

        OStim::ThreadInterface* _threads{ nullptr };
        NodeListener _nodeListener;
        std::atomic_bool _initialized{ false };
    };
}
