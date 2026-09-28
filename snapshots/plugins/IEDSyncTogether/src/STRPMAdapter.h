#pragma once

#include "PCH.h"
#include "LocalIEDState.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace IEDSyncTogether
{
    class STRPMAdapter
    {
    public:
        static STRPMAdapter& GetSingleton();
        ~STRPMAdapter();

        bool Start();
        void Stop();
        void Reset();
        void Publish(const LocalIEDState& state, std::string_view payload);

    private:
        struct RemoteSnapshot
        {
            LocalIEDState state;
            std::string displayName;
            std::uint64_t sequence{ 0 };
        };

        static void STRPM_CALL OnMessage(const STRPM::Message* message, void* userData);
        static void STRPM_CALL OnProxyMapping(const STRPM::ProxyMappingEvent* event, void* userData);

        bool LoadApi();
        std::string GetLocalPlayerName() const;
        void RetryLoop(std::stop_token token);
        void RetryTick();
        bool TrySend(const LocalIEDState& state, std::string_view payload, std::string_view reason);

        void QueueRemoteApply(STRPM::ConnectionID connectionID, bool force = false);
        void ApplyRemoteOnGameThread(STRPM::ConnectionID connectionID, bool force);
        void HandleProxyMappingOnGameThread(STRPM::ProxyMappingEvent event);

        std::atomic_bool _running{ false };
        const STRPM::Interface* _api{ nullptr };
        const STRPM::ProxyResolverInterface* _resolver{ nullptr };
        bool _proxyListenerRegistered{ false };
        STRPM::ListenerHandle _listener{};
        std::atomic<STRPM::ConnectionID> _localConnectionID{ 0 };
        std::mutex _sendMutex;

        mutable std::mutex _stateMutex;
        LocalIEDState _latestState;
        std::string _latestPayload;
        bool _pending{ false };
        bool _haveSuccessfulSend{ false };
        std::optional<STRPM::Result> _lastFailure;
        std::chrono::steady_clock::time_point _lastSuccessfulSend{};
        std::jthread _retryTimer;

        mutable std::mutex _remoteMutex;
        std::unordered_map<STRPM::ConnectionID, RemoteSnapshot> _remoteStates;
    };
}
