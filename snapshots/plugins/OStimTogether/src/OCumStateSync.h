#pragma once

#include "PCH.h"
#include "OStimAPI/ThreadEventListener.h"
#include "OStimAPI/ThreadInterface.h"

namespace OStimTogether
{
    class OCumStateSync
    {
    public:
        static OCumStateSync& GetSingleton();

        bool Initialize();
        void Reset();
        void Tick();

        // Publishes the local player's current CumOverlays snapshot without
        // modifying local OCum rendering. OCum 3D equip-object meshes are
        // intentionally local-only / unsupported on remote STR proxies.
        void SendLocalSnapshot(std::string_view reason);

    private:
        OCumStateSync() = default;

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

        struct MeshState
        {
            bool initialized{ false };
            bool vaginal{ false };
            bool anal{ false };
            bool overlayInitialized{ false };
            std::string overlaySignature;
        };

        void HandleStart(OStim::Thread* thread);
        void HandleStop(OStim::Thread* thread);
        void SendLocalObjectState(
            RE::PlayerCharacter* player,
            std::string_view type,
            bool equipped,
            std::string_view reason);

        static std::string HexEncode(std::string_view value);
        static std::string BuildOverlaySignature(
            const std::vector<std::string>& chunks);

        OStim::ThreadInterface* _threads{ nullptr };
        StartListener _startListener;
        StopListener _stopListener;
        std::atomic_bool _initialized{ false };
        std::unordered_set<std::int32_t> _activeThreads;
        std::unordered_map<RE::FormID, MeshState> _meshStates;
        std::chrono::steady_clock::time_point _nextPoll{};
    };
}
