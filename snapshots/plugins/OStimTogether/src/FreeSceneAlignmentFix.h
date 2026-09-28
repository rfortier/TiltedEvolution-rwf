#pragma once

#include "PCH.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"

namespace OStimTogether
{
    class FreeSceneAlignmentFix
    {
    public:
        static FreeSceneAlignmentFix& GetSingleton();
        bool Initialize();

    private:
        FreeSceneAlignmentFix() = default;

        class StartListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        class NodeListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        class StopListener final : public OStim::ThreadEventListener
        {
        public:
            void listen(OStim::Thread* thread) override;
        };

        struct FreeState
        {
            std::uint64_t generation{ 0 };
            bool free{ false };
        };

        void HandleStart(OStim::Thread* thread);
        void HandleNode(OStim::Thread* thread);
        void HandleStop(OStim::Thread* thread);

        void EnterFreeScene(
            OStim::Thread* thread,
            std::chrono::milliseconds releaseDelay,
            std::string reason);

        void ScheduleTranslationRelease(
            std::int32_t threadID,
            std::uint64_t generation,
            std::chrono::milliseconds delay,
            std::string reason);

        void ReleaseTranslations(
            std::int32_t threadID,
            std::uint64_t generation,
            std::string_view reason);

        OStim::ThreadInterface* _threads{ nullptr };
        StartListener _startListener;
        NodeListener _nodeListener;
        StopListener _stopListener;
        std::atomic_bool _initialized{ false };

        std::mutex _stateMutex;
        std::unordered_map<std::int32_t, FreeState> _states;
    };
}
