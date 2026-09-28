#pragma once

#include "PCH.h"
#include "STRPMApi.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"

namespace OStimTogether
{
    // Free-standing paired animations can carry their visible per-role
    // displacement in NPC Root [Root]. Skyrim Together owns the remote
    // TESObjectREFR position, so this component synchronizes ONLY the local
    // translation of that animated root. It never copies rotation/scale,
    // never writes world transforms or descendants, and never moves the actor
    // reference itself.
    class FreeSceneRootSync
    {
    public:
        static FreeSceneRootSync& GetSingleton();

        bool Initialize();
        bool StartTransport();
        void StopTransport();
        void Reset();

        // Called on Skyrim's game thread from VisualKeepAlive.
        void Tick();

    private:
        FreeSceneRootSync() = default;
        ~FreeSceneRootSync();

        FreeSceneRootSync(const FreeSceneRootSync&) = delete;
        FreeSceneRootSync& operator=(const FreeSceneRootSync&) = delete;

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

        struct RootTranslation
        {
            RE::NiPoint3 value{};

            [[nodiscard]] bool IsFinite() const noexcept;
        };

        struct RemoteState
        {
            std::int32_t remoteThreadID{ -1 };
            RootTranslation translation{};
            std::chrono::steady_clock::time_point received{};
            std::chrono::steady_clock::time_point lastLog{};
        };

        static void __cdecl OnRootMessage(
            const STRPMApi::Message* message,
            void* userData);

        void HandleRootMessage(const STRPMApi::Message& message);
        void StoreIncoming(
            STRPMApi::ConnectionID senderConnectionID,
            std::string_view payload);

        void HandleStart(OStim::Thread* thread);
        void HandleStop(OStim::Thread* thread);

        [[nodiscard]] bool IsFreeStandingThread(OStim::Thread* thread) const;
        [[nodiscard]] bool ThreadContainsActor(
            OStim::Thread* thread,
            RE::Actor* actor) const;

        static RE::NiAVObject* FindAnimatedRoot(RE::Actor* actor);
        static bool ApplyRootTranslation(
            RE::Actor* actor,
            const RootTranslation& translation,
            RE::NiPoint3& before,
            RE::NiPoint3& after);

        static std::optional<std::string> Field(
            std::string_view payload,
            std::string_view key);

        OStim::ThreadInterface* _threads{ nullptr };
        std::uint32_t _threadInterfaceVersion{ 0 };
        StartListener _startListener;
        StopListener _stopListener;
        std::atomic_bool _initialized{ false };

        const STRPMApi::Interface* _api{ nullptr };
        STRPMApi::ListenerHandle _rootListener{};
        std::atomic_bool _transportRunning{ false };

        std::int32_t _activePlayerThreadID{ -1 };
        std::chrono::steady_clock::time_point _lastSend{};
        std::chrono::steady_clock::time_point _lastSendLog{};
        std::chrono::steady_clock::time_point _lastTransportWarn{};

        std::unordered_map<STRPMApi::ConnectionID, RemoteState> _remoteStates;
    };
}
