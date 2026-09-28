#pragma once

#include "PCH.h"
#include "SceneCenter.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"

namespace OStimTogether
{
    // Legacy class name retained for source/package stability. The lock no
    // longer writes the real local PlayerCharacter. In a shared free-standing
    // scene it keeps only each STR remote-player proxy's logical TESObjectREFR
    // origin on the common scene center. The rendered proxy 3D/skeleton is not
    // touched, so OStim remains free to provide the visible per-role animation
    // displacement while STR's already-root-motion-displaced network sample is
    // not counted a second time.
    class FreeSceneSelfOriginLock
    {
    public:
        static FreeSceneSelfOriginLock& GetSingleton();

        bool Initialize();
        void Reset();
        void Tick();

    private:
        FreeSceneSelfOriginLock() = default;

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

        void HandleStart(OStim::Thread* thread);
        void HandleStop(OStim::Thread* thread);
        void QueueArmAfterStart(std::int32_t threadID);
        void ArmAfterStart(std::int32_t threadID);

        [[nodiscard]] bool IsFreeStandingThread(OStim::Thread* thread) const;
        [[nodiscard]] RE::PlayerCharacter* FindLocalPlayer(OStim::Thread* thread) const;
        [[nodiscard]] bool HasSTRRemoteParticipant(OStim::Thread* thread) const;

        OStim::ThreadInterface* _threads{ nullptr };
        std::uint32_t _threadInterfaceVersion{ 0 };
        StartListener _startListener;
        StopListener _stopListener;
        std::atomic_bool _initialized{ false };

        std::int32_t _activeThreadID{ -1 };
        SceneCenter _center{};
        std::chrono::steady_clock::time_point _lastLog{};
    };
}
