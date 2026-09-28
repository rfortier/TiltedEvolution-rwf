#pragma once

#include "NetworkProtocol.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace DAVSyncTogether
{
    class DAVNetworkService
    {
    public:
        static DAVNetworkService& GetSingleton();

        bool Start();
        void Stop();
        void SendArmorState(const WornArmorState& armor, std::string_view variant, bool unequipped = false);

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return _running.load();
        }

    private:
        struct ReceivedMessage
        {
            std::string payload;
            STRPM::ConnectionID connectionID{ 0 };
            std::string displayName;
            bool isHost{ false };
            std::uint64_t sequence{ 0 };
        };

        DAVNetworkService() = default;
        ~DAVNetworkService();
        DAVNetworkService(const DAVNetworkService&) = delete;
        DAVNetworkService& operator=(const DAVNetworkService&) = delete;

        static void STRPM_CALL OnMessage(const STRPM::Message* message, void* userData);
        void QueueReceivedMessage(ReceivedMessage message);
        void HandleReceivedMessageOnGameThread(ReceivedMessage message);
        void UpdateLocalDisplayName();

        static constexpr const char* kChannel = "DAVSyncTogether.State.v2";

        const STRPM::Interface* _api{ nullptr };
        const STRPM::ProxyResolverInterface* _resolver{ nullptr };
        STRPM::ListenerHandle _listener{};
        std::atomic_bool _running{ false };
    };
}
