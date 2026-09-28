#include "PCH.h"
#include "UdpTransport.h"

#include "FormIdentity.h"
#include "StrServerDiscovery.h"

#include <bcrypt.h>

namespace IEDSyncTogether
{
    namespace
    {
        constexpr std::string_view kDiscoveryPrefix = "IEDSTDISC|v1|HELLO|";
        constexpr std::string_view kGameplayPrefix = "IEDST|v1|";
        constexpr std::string_view kAuthField = "|auth=";

        std::optional<std::string> ComputeHmacSha256(
            std::string_view secret,
            std::string_view data)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hashHandle = nullptr;
            std::vector<UCHAR> hashObject;
            std::vector<UCHAR> digest;

            auto status = BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                BCRYPT_ALG_HANDLE_HMAC_FLAG);
            if (!BCRYPT_SUCCESS(status)) {
                return std::nullopt;
            }

            DWORD objectLength = 0;
            DWORD digestLength = 0;
            DWORD written = 0;
            status = BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &written,
                0);
            if (BCRYPT_SUCCESS(status)) {
                status = BCryptGetProperty(
                    algorithm,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&digestLength),
                    sizeof(digestLength),
                    &written,
                    0);
            }
            if (BCRYPT_SUCCESS(status)) {
                hashObject.resize(objectLength);
                digest.resize(digestLength);
                status = BCryptCreateHash(
                    algorithm,
                    &hashHandle,
                    hashObject.data(),
                    static_cast<ULONG>(hashObject.size()),
                    reinterpret_cast<PUCHAR>(const_cast<char*>(secret.data())),
                    static_cast<ULONG>(secret.size()),
                    0);
            }
            if (BCRYPT_SUCCESS(status)) {
                status = BCryptHashData(
                    hashHandle,
                    reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                    static_cast<ULONG>(data.size()),
                    0);
            }
            if (BCRYPT_SUCCESS(status)) {
                status = BCryptFinishHash(
                    hashHandle,
                    digest.data(),
                    static_cast<ULONG>(digest.size()),
                    0);
            }

            if (hashHandle) {
                BCryptDestroyHash(hashHandle);
            }
            BCryptCloseAlgorithmProvider(algorithm, 0);

            if (!BCRYPT_SUCCESS(status)) {
                return std::nullopt;
            }

            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            result.reserve(digest.size() * 2);
            for (const auto byte : digest) {
                result.push_back(hex[(byte >> 4) & 0x0F]);
                result.push_back(hex[byte & 0x0F]);
            }
            return result;
        }
    }

    UdpTransport& UdpTransport::GetSingleton()
    {
        static UdpTransport instance;
        return instance;
    }

    UdpTransport::~UdpTransport()
    {
        Stop();
    }

    std::string UdpTransport::SanitizeField(std::string value)
    {
        for (auto& ch : value) {
            if (ch == '|' || ch == '\r' || ch == '\n') {
                ch = '_';
            }
        }
        return value.empty() ? "Player" : value;
    }

    std::string UdpTransport::GetLocalPlayerName() const
    {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            const auto* name = player->GetName();
            if (name && *name) {
                return SanitizeField(name);
            }
        }

        std::array<char, MAX_COMPUTERNAME_LENGTH + 1> computerName{};
        DWORD length = static_cast<DWORD>(computerName.size());
        if (GetComputerNameA(computerName.data(), &length) && length > 0) {
            return SanitizeField(std::string(computerName.data(), length));
        }
        return "Player";
    }

    std::optional<std::string> UdpTransport::ReadField(
        std::string_view packet,
        std::string_view key)
    {
        const auto needle = fmt::format("{}=", key);
        auto position = packet.find(needle);
        if (position == std::string_view::npos) {
            return std::nullopt;
        }
        position += needle.size();
        auto end = packet.find('|', position);
        if (end == std::string_view::npos) {
            end = packet.size();
        }
        return std::string(packet.substr(position, end - position));
    }

    std::uint16_t UdpTransport::ParsePort(const std::optional<std::string>& value)
    {
        if (!value || value->empty()) {
            return 0;
        }
        try {
            const auto parsed = std::stoul(*value);
            if (parsed == 0 || parsed > 65535) {
                return 0;
            }
            return static_cast<std::uint16_t>(parsed);
        } catch (...) {
            return 0;
        }
    }

    std::string UdpTransport::AddressToString(const sockaddr_in& address)
    {
        std::array<char, INET_ADDRSTRLEN> ip{};
        InetNtopA(
            AF_INET,
            &address.sin_addr,
            ip.data(),
            static_cast<DWORD>(ip.size()));
        return fmt::format("{}:{}", ip.data(), ntohs(address.sin_port));
    }

    bool UdpTransport::SameEndpoint(const sockaddr_in& left, const sockaddr_in& right)
    {
        return left.sin_addr.s_addr == right.sin_addr.s_addr &&
               left.sin_port == right.sin_port;
    }

    std::string UdpTransport::RemoveAuthField(std::string_view packet)
    {
        const auto authPosition = packet.rfind(kAuthField);
        if (authPosition == std::string_view::npos) {
            return std::string(packet);
        }

        const auto nextField = packet.find('|', authPosition + kAuthField.size());
        std::string result(packet.substr(0, authPosition));
        if (nextField != std::string_view::npos) {
            result.append(packet.substr(nextField));
        }
        return result;
    }

    std::string UdpTransport::GetSharedSecretSnapshot() const
    {
        std::scoped_lock lock(_authMutex);
        return _sharedSecret;
    }

    std::string UdpTransport::SignPacket(std::string packet) const
    {
        packet = RemoveAuthField(packet);
        const auto sharedSecret = GetSharedSecretSnapshot();
        if (sharedSecret.empty()) {
            return packet;
        }

        const auto tag = ComputeHmacSha256(sharedSecret, packet);
        if (!tag) {
            SKSE::log::error("IEDSTNET HMAC generation failed");
            return {};
        }
        return fmt::format("{}|auth={}", packet, *tag);
    }

    bool UdpTransport::AuthenticatePacket(std::string_view packet) const
    {
        const auto sharedSecret = GetSharedSecretSnapshot();
        if (sharedSecret.empty()) {
            return true;
        }

        const auto authPosition = packet.rfind(kAuthField);
        if (authPosition == std::string_view::npos) {
            return false;
        }

        const auto tagStart = authPosition + kAuthField.size();
        const auto tagEnd = packet.find('|', tagStart);
        const auto supplied = packet.substr(
            tagStart,
            tagEnd == std::string_view::npos ? std::string_view::npos : tagEnd - tagStart);
        const auto expected = ComputeHmacSha256(sharedSecret, RemoveAuthField(packet));
        if (!expected || supplied.size() != expected->size()) {
            return false;
        }

        unsigned char difference = 0;
        for (std::size_t i = 0; i < supplied.size(); ++i) {
            difference |= static_cast<unsigned char>(supplied[i] ^ (*expected)[i]);
        }
        return difference == 0;
    }

    std::string UdpTransport::MarkRelayed(std::string_view packet) const
    {
        auto result = RemoveAuthField(packet);
        constexpr std::string_view localMarker = "|relay=0|";
        const auto marker = result.find(localMarker);
        if (marker != std::string::npos) {
            result.replace(marker, localMarker.size(), "|relay=1|");
        } else {
            result.insert(kGameplayPrefix.size(), "relay=1|");
        }
        return SignPacket(std::move(result));
    }

    std::optional<sockaddr_in> UdpTransport::ResolveRemotePeer(
        const Config::RemotePeer& peer) const
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo* addresses = nullptr;
        const auto port = std::to_string(peer.port);
        if (getaddrinfo(peer.host.c_str(), port.c_str(), &hints, &addresses) != 0 ||
            !addresses) {
            return std::nullopt;
        }

        std::optional<sockaddr_in> result;
        for (auto* item = addresses; item; item = item->ai_next) {
            if (item->ai_family == AF_INET &&
                item->ai_addrlen >= static_cast<int>(sizeof(sockaddr_in))) {
                result = *reinterpret_cast<sockaddr_in*>(item->ai_addr);
                break;
            }
        }
        freeaddrinfo(addresses);
        return result;
    }

    std::vector<sockaddr_in> UdpTransport::SnapshotConfiguredPeers() const
    {
        std::scoped_lock lock(_configuredPeerMutex);
        return _configuredPeers;
    }

    void UdpTransport::RefreshSkyrimTogetherAutoConfig(bool force)
    {
        if (!_config.autoRemoteFromSTR && !_config.autoSharedSecretFromSTR) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!force &&
            _lastStrAutoConfigRefresh.time_since_epoch().count() != 0 &&
            now - _lastStrAutoConfigRefresh < std::chrono::seconds(5)) {
            return;
        }
        _lastStrAutoConfigRefresh = now;

        if (_config.autoSharedSecretFromSTR && _config.sharedSecret.empty()) {
            const auto password = _config.relayMode ?
                StrServerDiscovery::ReadServerPasswordFromConfig() :
                StrServerDiscovery::ReadClientState(_config.autoRemotePort).password;
            if (password && !password->empty()) {
                bool changed = false;
                {
                    std::scoped_lock lock(_authMutex);
                    if (_sharedSecret != *password) {
                        _sharedSecret = *password;
                        changed = true;
                    }
                }
                if (changed) {
                    SKSE::log::info(
                        "IEDSTNET STR shared secret auto-loaded source={}",
                        _config.relayMode ? "STServer.ini" : "localStorage");
                }
            }
        }

        if (!_config.autoRemoteFromSTR || _config.relayMode) {
            return;
        }

        const auto state = StrServerDiscovery::ReadClientState(_config.autoRemotePort);
        if (!state.remotePeer) {
            if (force) {
                SKSE::log::info(
                    "IEDSTNET STR auto remote pending: no saved direct-connect address found");
            }
            return;
        }

        const auto resolved = ResolveRemotePeer(*state.remotePeer);
        if (!resolved) {
            SKSE::log::warn(
                "IEDSTNET STR auto remote resolution failed address=\"{}\" host=\"{}\" port={}",
                state.rawAddress,
                state.remotePeer->host,
                state.remotePeer->port);
            return;
        }

        const auto endpoint = AddressToString(*resolved);
        bool inserted = false;
        {
            std::scoped_lock lock(_configuredPeerMutex);
            const auto duplicate = std::ranges::any_of(
                _configuredPeers,
                [&](const sockaddr_in& existing) {
                    return SameEndpoint(existing, *resolved);
                });
            if (!duplicate) {
                _configuredPeers.push_back(*resolved);
                inserted = true;
            }
        }

        if (inserted) {
            SKSE::log::info(
                "IEDSTNET STR auto remote configured address=\"{}\" endpoint={}",
                state.rawAddress,
                endpoint);
        }
    }

    bool UdpTransport::SendPacketTo(
        std::string_view packet,
        const sockaddr_in& destination,
        std::string_view operation)
    {
        if (packet.empty() || _socket == INVALID_SOCKET) {
            return false;
        }

        std::scoped_lock lock(_sendMutex);
        const auto sent = sendto(
            _socket,
            packet.data(),
            static_cast<int>(packet.size()),
            0,
            reinterpret_cast<const sockaddr*>(&destination),
            sizeof(destination));
        if (sent == SOCKET_ERROR) {
            if (_running.load()) {
                SKSE::log::warn(
                    "IEDSTNET {} failed to {}: {}",
                    operation,
                    AddressToString(destination),
                    WSAGetLastError());
            }
            return false;
        }
        return true;
    }

    bool UdpTransport::Start(const Config& config, PacketHandler handler)
    {
        if (_running.load()) {
            return true;
        }
        if (!config.networkEnabled || !handler) {
            return false;
        }

        _config = config;
        _handler = std::move(handler);
        {
            std::scoped_lock lock(_authMutex);
            _sharedSecret = _config.sharedSecret;
        }
        _lastStrAutoConfigRefresh = {};

        WSADATA wsa{};
        if (const auto result = WSAStartup(MAKEWORD(2, 2), &wsa); result != 0) {
            SKSE::log::error("IEDSTNET Winsock initialization failed: {}", result);
            return false;
        }

        _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_socket == INVALID_SOCKET) {
            SKSE::log::error("IEDSTNET UDP socket creation failed: {}", WSAGetLastError());
            WSACleanup();
            return false;
        }

        BOOL enabled = TRUE;
        setsockopt(
            _socket,
            SOL_SOCKET,
            SO_BROADCAST,
            reinterpret_cast<const char*>(&enabled),
            sizeof(enabled));
        setsockopt(
            _socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<const char*>(&enabled),
            sizeof(enabled));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(_config.localPort);
        if (bind(_socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
            SKSE::log::error(
                "IEDSTNET UDP bind failed on port {}: {}",
                _config.localPort,
                WSAGetLastError());
            closesocket(_socket);
            _socket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }

        DWORD timeout = 250;
        setsockopt(
            _socket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout),
            sizeof(timeout));

        _broadcast = {};
        _broadcast.sin_family = AF_INET;
        _broadcast.sin_port = htons(_config.localPort);
        _broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);

        std::vector<sockaddr_in> configuredPeers;
        std::unordered_set<std::string> configuredEndpoints;
        for (const auto& peer : _config.remotePeers) {
            const auto resolved = ResolveRemotePeer(peer);
            if (!resolved) {
                SKSE::log::warn(
                    "IEDSTNET remote peer resolution failed host=\"{}\" port={}",
                    peer.host,
                    peer.port);
                continue;
            }

            const auto endpoint = AddressToString(*resolved);
            if (configuredEndpoints.insert(endpoint).second) {
                configuredPeers.push_back(*resolved);
                SKSE::log::info(
                    "IEDSTNET remote peer configured host=\"{}\" endpoint={}",
                    peer.host,
                    endpoint);
            }
        }
        {
            std::scoped_lock lock(_configuredPeerMutex);
            _configuredPeers = std::move(configuredPeers);
        }

        _instanceID = fmt::format("{:08X}-{:016X}", GetCurrentProcessId(), GetTickCount64());
        RefreshSkyrimTogetherAutoConfig(true);
        const auto configuredPeerCount = SnapshotConfiguredPeers().size();
        const auto sharedSecret = GetSharedSecretSnapshot();

        _running.store(true);
        _receiver = std::jthread([this](std::stop_token) { ReceiverLoop(); });
        if (_config.autoDiscovery ||
            configuredPeerCount > 0 ||
            _config.relayMode ||
            _config.autoRemoteFromSTR ||
            _config.autoSharedSecretFromSTR) {
            _maintenance = std::jthread([this](std::stop_token token) {
                MaintenanceLoop(token);
            });
        }

        SKSE::log::info(
            "IEDSTNET UDP started AUTO={} RELAY={} AUTH={} player=\"{}\" port={} configuredPeers={} instance={}",
            _config.autoDiscovery ? 1 : 0,
            _config.relayMode ? 1 : 0,
            sharedSecret.empty() ? 0 : 1,
            GetLocalPlayerName(),
            _config.localPort,
            configuredPeerCount,
            _instanceID);

        if (_config.relayMode && sharedSecret.empty()) {
            SKSE::log::warn(
                "IEDSTNET relay is unauthenticated; set Network/SharedSecret before exposing UDP port {} to the Internet",
                _config.localPort);
        }

        SendHello();
        return true;
    }

    void UdpTransport::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_receiver.joinable()) {
            _receiver.request_stop();
        }
        if (_maintenance.joinable()) {
            _maintenance.request_stop();
        }
        if (_socket != INVALID_SOCKET) {
            closesocket(_socket);
            _socket = INVALID_SOCKET;
        }
        if (_receiver.joinable()) {
            _receiver.join();
        }
        if (_maintenance.joinable()) {
            _maintenance.join();
        }

        {
            std::scoped_lock lock(_peerMutex);
            _peers.clear();
        }
        {
            std::scoped_lock lock(_configuredPeerMutex);
            _configuredPeers.clear();
        }
        {
            std::scoped_lock lock(_authMutex);
            _sharedSecret.clear();
        }
        _handler = {};
        WSACleanup();
        SKSE::log::info("IEDSTNET UDP stopped");
    }

    void UdpTransport::SendHello()
    {
        if (!_running.load() || _socket == INVALID_SOCKET) {
            return;
        }

        if (_config.autoDiscovery) {
            SendHelloTo(_broadcast, false);
        }
        for (const auto& peer : SnapshotConfiguredPeers()) {
            SendHelloTo(peer, true);
        }
    }

    void UdpTransport::SendHelloTo(
        const sockaddr_in& destination,
        bool useObservedSourcePort)
    {
        const auto packet = SignPacket(fmt::format(
            "IEDSTDISC|v1|HELLO|id={}|name={}|port={}|observed={}",
            _instanceID,
            HexEncode(GetLocalPlayerName()),
            _config.localPort,
            useObservedSourcePort ? 1 : 0));
        SendPacketTo(packet, destination, "discovery TX");
    }

    void UdpTransport::RegisterPeer(
        const sockaddr_in& source,
        std::uint16_t advertisedPort,
        std::string_view name,
        std::string_view instanceID)
    {
        if (instanceID.empty() || instanceID == _instanceID) {
            return;
        }

        sockaddr_in peerAddress = source;
        peerAddress.sin_port = htons(advertisedPort);
        const auto key = fmt::format("{}|{}", instanceID, AddressToString(peerAddress));

        bool inserted = false;
        {
            std::scoped_lock lock(_peerMutex);
            auto [iterator, isNew] = _peers.try_emplace(key);
            inserted = isNew;
            iterator->second.address = peerAddress;
            iterator->second.name = std::string(name);
            iterator->second.instanceID = std::string(instanceID);
            iterator->second.lastSeen = std::chrono::steady_clock::now();
        }

        if (inserted) {
            SKSE::log::info(
                "IEDSTNET DISCOVERED peer=\"{}\" addr={} instance={}",
                name,
                AddressToString(peerAddress),
                instanceID);
            SendHelloTo(peerAddress, true);
        }
    }

    bool UdpTransport::HandleDiscovery(
        std::string_view packet,
        const sockaddr_in& source)
    {
        if (!packet.starts_with(kDiscoveryPrefix)) {
            return false;
        }

        const auto id = ReadField(packet, "id");
        const auto encodedName = ReadField(packet, "name");
        const auto advertisedPort = ParsePort(ReadField(packet, "port"));
        const auto observed = ReadField(packet, "observed");
        const auto port = observed && *observed == "1" ?
            ntohs(source.sin_port) : advertisedPort;

        if (!id || !encodedName || port == 0) {
            SKSE::log::warn(
                "IEDSTNET malformed discovery packet from {}",
                AddressToString(source));
            return true;
        }

        auto name = HexDecode(*encodedName);
        RegisterPeer(source, port, name.value_or("Peer"), *id);
        return true;
    }

    void UdpTransport::TouchGameplayPeer(
        std::string_view packet,
        const sockaddr_in& source)
    {
        if (!packet.starts_with(kGameplayPrefix)) {
            return;
        }

        const auto sender = ReadField(packet, "from");
        const auto name = sender ? HexDecode(*sender) : std::nullopt;
        const auto key = fmt::format("gameplay|{}", AddressToString(source));

        std::scoped_lock lock(_peerMutex);
        auto& peer = _peers[key];
        peer.address = source;
        peer.name = name.value_or("Peer");
        peer.instanceID = key;
        peer.lastSeen = std::chrono::steady_clock::now();
    }

    void UdpTransport::ExpirePeers()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::milliseconds(_config.peerTimeoutMs);
        std::vector<std::string> expired;

        {
            std::scoped_lock lock(_peerMutex);
            for (auto iterator = _peers.begin(); iterator != _peers.end();) {
                if (now - iterator->second.lastSeen > timeout) {
                    expired.push_back(fmt::format(
                        "\"{}\" {}",
                        iterator->second.name,
                        AddressToString(iterator->second.address)));
                    iterator = _peers.erase(iterator);
                } else {
                    ++iterator;
                }
            }
        }

        for (const auto& peer : expired) {
            SKSE::log::info("IEDSTNET PEER EXPIRED {}", peer);
        }
    }

    std::vector<sockaddr_in> UdpTransport::SnapshotDestinations(
        const sockaddr_in* excluded)
    {
        std::vector<sockaddr_in> result;
        std::unordered_set<std::string> seen;

        for (const auto& peer : SnapshotConfiguredPeers()) {
            if (excluded && SameEndpoint(peer, *excluded)) {
                continue;
            }
            const auto endpoint = AddressToString(peer);
            if (seen.insert(endpoint).second) {
                result.push_back(peer);
            }
        }

        std::scoped_lock lock(_peerMutex);
        for (const auto& [key, peer] : _peers) {
            if (excluded && SameEndpoint(peer.address, *excluded)) {
                continue;
            }
            const auto endpoint = AddressToString(peer.address);
            if (seen.insert(endpoint).second) {
                result.push_back(peer.address);
            }
        }

        return result;
    }

    void UdpTransport::Send(std::string_view payload)
    {
        if (!_running.load() || _socket == INVALID_SOCKET || payload.empty()) {
            return;
        }

        const auto packet = SignPacket(fmt::format(
            "IEDST|v1|id={}|from={}|relay=0|{}",
            _instanceID,
            HexEncode(GetLocalPlayerName()),
            payload));
        if (packet.empty()) {
            return;
        }

        auto destinations = SnapshotDestinations();
        if (destinations.empty() && _config.autoDiscovery) {
            destinations.push_back(_broadcast);
        }
        if (destinations.empty()) {
            SKSE::log::warn("IEDSTNET TX dropped: no peer available");
            return;
        }

        std::size_t sentCount = 0;
        for (const auto& destination : destinations) {
            if (SendPacketTo(packet, destination, "TX")) {
                ++sentCount;
            }
        }

        SKSE::log::info("IEDSTNET TX peers={} {}", sentCount, packet);
    }

    void UdpTransport::RelayGameplayPacket(
        std::string_view packet,
        const sockaddr_in& source)
    {
        if (!_config.relayMode || !packet.starts_with(kGameplayPrefix)) {
            return;
        }

        const auto relay = ReadField(packet, "relay");
        if (relay && *relay != "0") {
            return;
        }

        const auto relayedPacket = MarkRelayed(packet);
        if (relayedPacket.empty()) {
            return;
        }

        const auto destinations = SnapshotDestinations(&source);
        std::size_t sentCount = 0;
        for (const auto& destination : destinations) {
            if (SendPacketTo(relayedPacket, destination, "RELAY")) {
                ++sentCount;
            }
        }

        auto sender = ReadField(packet, "from");
        auto decodedSender = sender ? HexDecode(*sender) : std::nullopt;
        SKSE::log::info(
            "IEDSTNET RELAY source={} peers={} sender=\"{}\"",
            AddressToString(source),
            sentCount,
            decodedSender.value_or("Peer"));
    }

    void UdpTransport::MaintenanceLoop(std::stop_token token)
    {
        while (!token.stop_requested() && _running.load()) {
            RefreshSkyrimTogetherAutoConfig(false);
            SendHello();
            ExpirePeers();

            const auto interval = std::chrono::milliseconds(_config.discoveryIntervalMs);
            auto elapsed = std::chrono::milliseconds(0);
            while (elapsed < interval && !token.stop_requested() && _running.load()) {
                constexpr auto slice = std::chrono::milliseconds(100);
                std::this_thread::sleep_for(slice);
                elapsed += slice;
            }
        }
    }

    void UdpTransport::ReceiverLoop()
    {
        std::array<char, 8192> buffer{};
        while (_running.load()) {
            sockaddr_in source{};
            int sourceLength = sizeof(source);
            const auto received = recvfrom(
                _socket,
                buffer.data(),
                static_cast<int>(buffer.size() - 1),
                0,
                reinterpret_cast<sockaddr*>(&source),
                &sourceLength);

            if (received == SOCKET_ERROR) {
                const auto error = WSAGetLastError();
                if (!_running.load()) {
                    break;
                }
                if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                    continue;
                }
                SKSE::log::warn("IEDSTNET UDP receive failed: {}", error);
                continue;
            }
            if (received <= 0) {
                continue;
            }

            const std::string packet(buffer.data(), static_cast<std::size_t>(received));
            const bool discoveryPacket = packet.starts_with(kDiscoveryPrefix);
            const bool gameplayPacket = packet.starts_with(kGameplayPrefix);
            if (!discoveryPacket && !gameplayPacket) {
                continue;
            }

            if (!AuthenticatePacket(packet)) {
                SKSE::log::warn(
                    "IEDSTNET RX authentication failed from {}",
                    AddressToString(source));
                continue;
            }

            if (HandleDiscovery(packet, source)) {
                continue;
            }

            if (gameplayPacket) {
                const auto id = ReadField(packet, "id");
                if (id && *id == _instanceID) {
                    continue;
                }
            }

            TouchGameplayPeer(packet, source);
            RelayGameplayPacket(packet, source);

            if (_handler) {
                _handler(packet);
            }
        }
    }
}
