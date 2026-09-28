#pragma once
#include "PCH.h"
#include "SceneCenter.h"
#include "OStimAPI/Thread.h"

namespace OStimTogether
{
    class NetworkProbe
    {
    public:
        static NetworkProbe& GetSingleton();

        void SceneStart(OStim::Thread* thread);
        void SceneNode(OStim::Thread* thread);
        void SceneSpeed(
            OStim::Thread* thread,
            std::int32_t speed);
        void SceneStop(OStim::Thread* thread);

    private:
        NetworkProbe() = default;

        std::string BuildActorList(OStim::Thread* thread) const;
        std::string BuildActorPoses(
            OStim::Thread* thread,
            const SceneCenter& center) const;

        FurnitureAnchor FindLockedSceneFurniture(
            OStim::Thread* thread) const;

        static std::string EncodeFurniture(
            const FurnitureAnchor& furniture);

        std::string GetNodeID(OStim::Thread* thread) const;

        std::mutex _mutex;
        std::unordered_set<std::int32_t> _startedThreads;
    };
}
