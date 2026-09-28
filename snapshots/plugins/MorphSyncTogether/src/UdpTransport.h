#pragma once

#include "PCH.h"
#include "STRPMCompat.h"

namespace MorphSyncTogether
{
    // Compatibility name retained to minimize churn in the proven MorphSync
    // service. On the strpm branch this class no longer opens any UDP socket:
    // it is a thin consumer of STRPluginMessagingAPI + ProxyResolver.
    class UdpTransport
    {
    public:
        static UdpTransport& GetSingleton();

        bool Start();
        void Stop();
        void Send(std::string_view payload);

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return _running.load();
        }

        [[nodiscard]] std::string GetLocalClientName() const;
        [[nodiscard]] RE::Actor* ResolveProxyBySender(std::string_view sender) const;

    private:
        UdpTransport() = default;
        ~UdpTransport();
        UdpTransport(const UdpTransport&) = delete;
        UdpTransport& operator=(const UdpTransport&) = delete;

        static void STRPM_CALL OnMessage(const STRPM::Message* message, void* userData);
        void HandleMessage(const STRPM::Message& message);
        void QueueMessage(std::string packet);

        static std::string SanitizeField(std::string value);
        static const char* ResultName(STRPM::Result result) noexcept;

        const STRPM::Interface* _api{ nullptr };
        const STRPM::ProxyResolverInterface* _resolver{ nullptr };
        STRPM::ListenerHandle _listener{};
        HMODULE _module{ nullptr };

        std::atomic_bool _running{ false };
        mutable std::mutex _senderMutex;
        std::unordered_map<std::string, STRPM::ConnectionID> _senderConnections;
        std::string _localName;
    };
}
