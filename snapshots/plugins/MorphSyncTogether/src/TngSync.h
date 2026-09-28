#pragma once

#include "PCH.h"

namespace MorphSyncTogether
{
    class TngSync
    {
    public:
        static TngSync& GetSingleton();

        void Start();
        void Stop();
        void Reset();

        // Receives the payload starting at TNGSIZE, e.g. TNGSIZE|scale=1.25.
        // Must be called on Skyrim's game thread.
        void HandlePacket(std::string_view sender, std::string_view payload);

    private:
        struct RemoteState
        {
            float scale{ 1.0F };
            RE::FormID lastActorFormID{ 0 };
            bool everApplied{ false };
        };

        TngSync() = default;
        ~TngSync();
        TngSync(const TngSync&) = delete;
        TngSync& operator=(const TngSync&) = delete;

        void SyncLoop(std::stop_token stopToken);
        void QueueTick();
        void TickOnGameThread();
        void TryApplyRemote(const std::string& sender, bool force);

        static bool IntegrationEnabled();
        static std::optional<float> CaptureScale(RE::Actor* actor);
        static std::optional<float> ParseScale(std::string_view payload);
        static bool NearlyEqual(float lhs, float rhs);

        std::jthread _syncThread;
        std::atomic_bool _running{ false };
        std::atomic_bool _tickQueued{ false };

        float _lastSentScale{ 0.0F };
        bool _hasLastSentScale{ false };
        std::chrono::steady_clock::time_point _lastSentAt{};

        mutable std::mutex _remoteMutex;
        std::unordered_map<std::string, RemoteState> _remoteStates;
    };
}
