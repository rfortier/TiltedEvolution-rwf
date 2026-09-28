#pragma once

#include "PCH.h"
#include "Config.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace IEDSyncTogether
{
    class UdpTransport
    {
    public:
        using PacketHandler = std::function<void(std::string)>;

        static UdpTransport& GetSingleton();

        bool Start(const Config& config, PacketHandler handler);
        void Stop();
        void Send(std::string_view payload);

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return _running.load();
        }

        [[nodiscard]] std::string GetLocalPlayerName() const;

    private:
        struct Peer
        {
            sockaddr_in address{};
            std::string name;
            std::string instanceID;
            std::chrono::steady_clock::time_point lastSeen{};
        };

        UdpTransport() = default;
        ~UdpTransport();
        UdpTransport(const UdpTransport&) = delete;
        UdpTransport& operator=(const UdpTransport&) = delete;

        void ReceiverLoop();
        void MaintenanceLoop(std::stop_token token);
        void SendHello();
        void SendHelloTo(const sockaddr_in& destination, bool useObservedSourcePort);
        bool HandleDiscovery(std::string_view packet, const sockaddr_in& source);
        void RegisterPeer(
            const sockaddr_in& source,
            std::uint16_t advertisedPort,
            std::string_view name,
            std::string_view instanceID);
        void TouchGameplayPeer(std::string_view packet, const sockaddr_in& source);
        void ExpirePeers();
        std::vector<sockaddr_in> SnapshotDestinations(const sockaddr_in* excluded = nullptr);
        std::vector<sockaddr_in> SnapshotConfiguredPeers() const;
        std::optional<sockaddr_in> ResolveRemotePeer(const Config::RemotePeer& peer) const;
        void RefreshSkyrimTogetherAutoConfig(bool force);
        bool SendPacketTo(
            std::string_view packet,
            const sockaddr_in& destination,
            std::string_view operation);
        void RelayGameplayPacket(std::string_view packet, const sockaddr_in& source);

        std::string GetSharedSecretSnapshot() const;
        std::string SignPacket(std::string packet) const;
        bool AuthenticatePacket(std::string_view packet) const;
        std::string MarkRelayed(std::string_view packet) const;

        static std::string SanitizeField(std::string value);
        static std::optional<std::string> ReadField(
            std::string_view packet,
            std::string_view key);
        static std::uint16_t ParsePort(const std::optional<std::string>& value);
        static std::string RemoveAuthField(std::string_view packet);
        static std::string AddressToString(const sockaddr_in& address);
        static bool SameEndpoint(const sockaddr_in& left, const sockaddr_in& right);

        Config _config{};
        PacketHandler _handler;
        SOCKET _socket{ INVALID_SOCKET };
        sockaddr_in _broadcast{};
        std::vector<sockaddr_in> _configuredPeers;
        mutable std::mutex _configuredPeerMutex;
        mutable std::mutex _authMutex;
        std::string _sharedSecret;
        std::chrono::steady_clock::time_point _lastStrAutoConfigRefresh{};
        std::string _instanceID;

        std::jthread _receiver;
        std::jthread _maintenance;
        std::atomic_bool _running{ false };
        std::mutex _sendMutex;
        mutable std::mutex _peerMutex;
        std::unordered_map<std::string, Peer> _peers;
    };
}
