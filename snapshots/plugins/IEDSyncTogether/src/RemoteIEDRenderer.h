#pragma once

#include "PCH.h"
#include "LocalIEDState.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace IEDSyncTogether
{
    class RemoteIEDRenderer
    {
    public:
        static RemoteIEDRenderer& GetSingleton();

        void Start();

        bool Apply(
            STRPM::ConnectionID connectionID,
            STRPM::ProxyFormID proxyFormID,
            std::string_view displayName,
            const LocalIEDState& state,
            bool force = false);

        void ClearProxy(STRPM::ProxyFormID proxyFormID);
        void Reset();

    private:
        struct TrackedRemoteObject
        {
            std::string itemName;
            std::string kind;
            std::optional<std::size_t> slot;
            RE::FormID remoteFormID{ 0 };
            std::string expectedAttachment;
            CapturedIEDObject object;
            bool acquired{ false };
            std::uint32_t corrections{ 0 };
        };

        struct TrackedProxyState
        {
            STRPM::ConnectionID connectionID{ 0 };
            std::string displayName;
            std::vector<TrackedRemoteObject> objects;
        };

        RemoteIEDRenderer() = default;

        void WatchdogLoop(std::stop_token stopToken);
        void ScheduleWatchdogTick();
        void WatchdogTick();

        std::unordered_map<STRPM::ProxyFormID, std::string> _appliedSignatures;
        std::unordered_map<STRPM::ProxyFormID, TrackedProxyState> _trackedProxies;
        std::jthread _watchdogThread;
        std::atomic_bool _watchdogTaskQueued{ false };
    };
}
