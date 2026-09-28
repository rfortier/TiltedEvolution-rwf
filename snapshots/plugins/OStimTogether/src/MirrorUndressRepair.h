#pragma once

#include "PCH.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"

namespace OStimTogether
{
    class MirrorUndressRepair
    {
    public:
        static MirrorUndressRepair& GetSingleton();
        bool Initialize();

    private:
        class StartListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        class StopListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        struct ResidualSnapshot
        {
            RE::FormID actorFormID{ 0 };
            std::vector<RE::FormID> items;
        };

        MirrorUndressRepair() = default;
        void HandleStart(OStim::Thread* thread);
        void HandleStop(OStim::Thread* thread);
        void Repair(std::int32_t threadID);
        void ForceResidualUnequip(std::int32_t threadID);
        void RestoreResidual(std::int32_t threadID);

        OStim::ThreadInterface* _threads{ nullptr };
        StartListener _startListener;
        StopListener _stopListener;
        std::atomic_bool _initialized{ false };

        std::mutex _mutex;
        std::unordered_map<std::int32_t, ResidualSnapshot> _residualByThread;
    };
}
