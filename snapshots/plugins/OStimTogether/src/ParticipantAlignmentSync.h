#pragma once

#include "PCH.h"
#include "STRPMApi.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"
#include "OStimAPI/ModThreadControl.h"

namespace OStimTogether
{
    // Ordinary free-standing scenes can contain two different Skyrim forms for
    // the same human participant: the real local PlayerCharacter on one client
    // and a dynamic STR proxy on every other client. OStim's alignment cache is
    // keyed from actor characteristics, so those two forms are not guaranteed
    // to resolve to the same ActorAlignmentData.
    //
    // Each real player is therefore authoritative only for their own OStim
    // alignment values. The values are broadcast once per free-standing node
    // and applied through OStim's public SetActorAlignment() API to the sender's
    // proxy on the receiving client. No skeleton, 3D-root or direct reference
    // write is performed here.
    class ParticipantAlignmentSync
    {
    public:
        static ParticipantAlignmentSync& GetSingleton();

        bool Initialize();
        bool StartTransport();
        void StopTransport();
        void Reset();

    private:
        ParticipantAlignmentSync() = default;
        ~ParticipantAlignmentSync();

        ParticipantAlignmentSync(const ParticipantAlignmentSync&) = delete;
        ParticipantAlignmentSync& operator=(const ParticipantAlignmentSync&) = delete;

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

        struct AlignmentPacket
        {
            std::string nodeID;
            OStimModAPI::Thread::ActorAlignmentData alignment{};
        };

        bool LoadOStimAPIs();
        bool IsFreeStandingThread(OStim::Thread* thread) const;
        std::optional<std::uint32_t> FindActorIndex(
            OStim::Thread* thread,
            RE::Actor* actor) const;

        void HandleStart(OStim::Thread* thread);
        void HandleNode(OStim::Thread* thread);
        void HandleStop(OStim::Thread* thread);
        void QueueBroadcast(std::int32_t localThreadID, std::chrono::milliseconds delay);
        void BroadcastNow(std::int32_t localThreadID);

        static void __cdecl OnMessage(const STRPMApi::Message* message, void* userData);
        void HandleMessage(const STRPMApi::Message& message);
        void ApplyIncoming(
            STRPMApi::ConnectionID senderConnectionID,
            AlignmentPacket packet,
            std::uint32_t attempt);
        void QueueApplyRetry(
            STRPMApi::ConnectionID senderConnectionID,
            AlignmentPacket packet,
            std::uint32_t attempt,
            std::chrono::milliseconds delay);

        static std::optional<std::string> Field(
            std::string_view payload,
            std::string_view key);
        static std::optional<float> ParseFloat(
            std::string_view payload,
            std::string_view key);
        static bool IsFiniteAlignment(
            const OStimModAPI::Thread::ActorAlignmentData& value) noexcept;

        OStim::ThreadInterface* _threads{ nullptr };
        OStimModAPI::Thread::IThreadInterface* _threadControl{ nullptr };
        std::uint32_t _threadInterfaceVersion{ 0 };

        StartListener _startListener;
        NodeListener _nodeListener;
        StopListener _stopListener;
        std::atomic_bool _initialized{ false };

        const STRPMApi::Interface* _api{ nullptr };
        STRPMApi::ListenerHandle _listener{};
        std::atomic_bool _transportRunning{ false };

        std::unordered_map<std::int32_t, std::string> _lastBroadcastNode;
        std::unordered_map<std::int32_t, std::chrono::steady_clock::time_point>
            _lastBroadcastAt;
    };
}
