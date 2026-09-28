#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#ifndef STRPM_ENABLE_UDP_BACKEND
#define STRPM_ENABLE_UDP_BACKEND 0
#endif

namespace
{
    constexpr std::string_view kDiscoveryPrefix = "STRPM|v1|HELLO|";
    constexpr std::string_view kDataPrefix = "STRPM|v1|DATA|";
    constexpr std::string_view kAuthField = "|auth=";
    constexpr std::size_t kMaxDatagramBytes = 65000;
    constexpr wchar_t kDefaultStrBridgeModule[] =
        L"Data\\SkyrimTogetherReborn\\STRPluginMessagingBridge.dll";

    struct SKSEInterface;

    struct PluginInfo
    {
        std::uint32_t infoVersion;
        const char* name;
        std::uint32_t version;
    };

    struct Config
    {
        bool enabled{ true };
        STRPM::RuntimeBackendMode backendMode{ STRPM::RuntimeBackendMode::kAuto };
        bool autoDiscovery{ true };
        bool relayMode{ false };
        bool requireKnownPeer{ true };
        std::uint16_t localPort{ 27990 };
        std::uint32_t discoveryIntervalMs{ 1500 };
        std::uint32_t peerTimeoutMs{ 15000 };
        std::wstring strBridgeModule{ kDefaultStrBridgeModule };
        std::string displayName;
        std::string sharedSecret;
        std::vector<sockaddr_in> remotePeers;
    };

    struct Listener
    {
        STRPM::ListenerHandle handle{};
        std::string channel;
        STRPM::ReceiveCallback callback{ nullptr };
        void* userData{ nullptr };
    };

    struct Peer
    {
        sockaddr_in address{};
        STRPM::ConnectionID connectionID{ 0 };
        std::string instanceID;
        std::string displayName;
        std::chrono::steady_clock::time_point lastSeen{};
    };

    std::string HexEncode(std::string_view value)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.size() * 2);
        for (const auto c : value) {
            const auto byte = static_cast<unsigned char>(c);
            encoded.push_back(digits[(byte >> 4) & 0x0F]);
            encoded.push_back(digits[byte & 0x0F]);
        }
        return encoded;
    }

    int HexValue(char c)
    {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + c - 'a';
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + c - 'A';
        }
        return -1;
    }

    std::optional<std::string> HexDecode(std::string_view value)
    {
        if ((value.size() % 2) != 0) {
            return std::nullopt;
        }

        std::string decoded;
        decoded.reserve(value.size() / 2);
        for (std::size_t i = 0; i < value.size(); i += 2) {
            const auto high = HexValue(value[i]);
            const auto low = HexValue(value[i + 1]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            decoded.push_back(static_cast<char>((high << 4) | low));
        }
        return decoded;
    }

    std::optional<std::string> ReadField(
        std::string_view packet,
        std::string_view key)
    {
        std::size_t start = 0;
        while (start <= packet.size()) {
            auto end = packet.find('|', start);
            if (end == std::string_view::npos) {
                end = packet.size();
            }

            const auto token = packet.substr(start, end - start);
            if (token.size() > key.size() &&
                token.starts_with(key) &&
                token[key.size()] == '=') {
                return std::string(token.substr(key.size() + 1));
            }

            if (end == packet.size()) {
                break;
            }
            start = end + 1;
        }
        return std::nullopt;
    }

    std::string RemoveAuthField(std::string_view packet)
    {
        const auto authPos = packet.rfind(kAuthField);
        if (authPos == std::string_view::npos) {
            return std::string(packet);
        }

        const auto nextField = packet.find('|', authPos + kAuthField.size());
        std::string result(packet.substr(0, authPos));
        if (nextField != std::string_view::npos) {
            result.append(packet.substr(nextField));
        }
        return result;
    }

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

        DWORD objectLength = 0;
        DWORD digestLength = 0;
        DWORD written = 0;
        if (BCRYPT_SUCCESS(status)) {
            status = BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &written,
                0);
        }
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

        if (hashHandle != nullptr) {
            BCryptDestroyHash(hashHandle);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }

        if (!BCRYPT_SUCCESS(status)) {
            return std::nullopt;
        }

        std::string result;
        result.reserve(digest.size() * 2);
        static constexpr char digits[] = "0123456789abcdef";
        for (const auto byte : digest) {
            result.push_back(digits[(byte >> 4) & 0x0F]);
            result.push_back(digits[byte & 0x0F]);
        }
        return result;
    }

    bool EqualsInsensitive(std::string_view left, std::string_view right)
    {
        return left.size() == right.size() &&
            std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                    std::tolower(static_cast<unsigned char>(b));
            });
    }

    STRPM::RuntimeBackendMode ParseBackendMode(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (value == "str" ||
            value == "strbridge" ||
            value == "str-bridge" ||
            value == "internal") {
            return STRPM::RuntimeBackendMode::kStrBridge;
        }
        if (value == "udp" ||
            value == "standalone") {
            return STRPM::RuntimeBackendMode::kUdp;
        }
        return STRPM::RuntimeBackendMode::kAuto;
    }

    std::wstring WidenUtf8Lossy(std::string_view value)
    {
        if (value.empty()) {
            return {};
        }

        const auto required = MultiByteToWideChar(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);
        if (required <= 0) {
            return {};
        }

        std::wstring result(static_cast<std::size_t>(required), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required);
        return result;
    }

    std::uint64_t ParseUInt64(std::string_view text)
    {
        std::uint64_t result = 0;
        const auto* first = text.data();
        const auto* last = text.data() + text.size();
        const auto parsed = std::from_chars(first, last, result);
        return parsed.ec == std::errc{} ? result : 0;
    }

    std::uint32_t ParseUInt32(std::string_view text)
    {
        std::uint32_t result = 0;
        const auto* first = text.data();
        const auto* last = text.data() + text.size();
        const auto parsed = std::from_chars(first, last, result);
        return parsed.ec == std::errc{} ? result : 0;
    }

    std::string AddressToString(const sockaddr_in& address)
    {
        char ip[INET_ADDRSTRLEN]{};
        InetNtopA(AF_INET, &address.sin_addr, ip, sizeof(ip));
        return std::string(ip) + ":" + std::to_string(ntohs(address.sin_port));
    }

    bool SameEndpoint(const sockaddr_in& left, const sockaddr_in& right)
    {
        return left.sin_addr.s_addr == right.sin_addr.s_addr &&
            left.sin_port == right.sin_port;
    }

    std::string SanitizeDisplayName(std::string value)
    {
        for (auto& c : value) {
            if (c == '|' || c == '\r' || c == '\n') {
                c = '_';
            }
        }
        if (value.empty()) {
            value = "Player";
        }
        return value;
    }

    std::string GetComputerDisplayName()
    {
        char computerName[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD length = static_cast<DWORD>(std::size(computerName));
        if (GetComputerNameA(computerName, &length) && length > 0) {
            return SanitizeDisplayName(std::string(computerName, length));
        }
        return "Player";
    }

    std::string ReadIniString(
        const char* path,
        const char* section,
        const char* key,
        const char* fallback)
    {
        std::array<char, 512> buffer{};
        GetPrivateProfileStringA(
            section,
            key,
            fallback,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            path);
        return std::string(buffer.data());
    }

#if STRPM_ENABLE_UDP_BACKEND
    std::optional<sockaddr_in> ParseEndpoint(
        std::string_view value,
        std::uint16_t defaultPort)
    {
        if (value.empty()) {
            return std::nullopt;
        }

        const auto separator = value.rfind(':');
        const auto host = separator == std::string_view::npos ?
            std::string(value) :
            std::string(value.substr(0, separator));
        const auto portText = separator == std::string_view::npos ?
            std::string{} :
            std::string(value.substr(separator + 1));

        std::uint32_t port = defaultPort;
        if (!portText.empty()) {
            port = ParseUInt32(portText);
        }
        if (host.empty() || port == 0 || port > 65535) {
            return std::nullopt;
        }

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo* addresses = nullptr;
        const auto portString = std::to_string(port);
        if (getaddrinfo(host.c_str(), portString.c_str(), &hints, &addresses) != 0) {
            return std::nullopt;
        }

        std::optional<sockaddr_in> result;
        for (auto* item = addresses; item != nullptr; item = item->ai_next) {
            if (item->ai_family == AF_INET &&
                item->ai_addrlen >= static_cast<int>(sizeof(sockaddr_in))) {
                result = *reinterpret_cast<sockaddr_in*>(item->ai_addr);
                break;
            }
        }
        freeaddrinfo(addresses);
        return result;
    }
#endif

    Config LoadConfig()
    {
        constexpr char path[] = "Data\\SKSE\\Plugins\\STRPluginMessagingAPI.ini";

        Config config{};
        config.enabled = GetPrivateProfileIntA("Network", "Enabled", 1, path) != 0;
        config.backendMode =
            ParseBackendMode(ReadIniString(path, "Transport", "Mode", "Auto"));
        config.autoDiscovery =
            GetPrivateProfileIntA("Network", "AutoDiscovery", 1, path) != 0;
        config.relayMode = GetPrivateProfileIntA("Network", "RelayMode", 0, path) != 0;
        config.requireKnownPeer =
            GetPrivateProfileIntA("Network", "RequireKnownPeer", 1, path) != 0;
        config.localPort = static_cast<std::uint16_t>(
            GetPrivateProfileIntA("Network", "LocalPort", 27990, path));
        config.discoveryIntervalMs =
            GetPrivateProfileIntA("Network", "DiscoveryIntervalMs", 1500, path);
        config.peerTimeoutMs =
            GetPrivateProfileIntA("Network", "PeerTimeoutMs", 15000, path);
        config.displayName =
            SanitizeDisplayName(ReadIniString(path, "Identity", "DisplayName", ""));
        config.sharedSecret = ReadIniString(path, "Security", "SharedSecret", "");
        if (const auto bridgeModule =
                WidenUtf8Lossy(ReadIniString(
                    path,
                    "Transport",
                    "STRBridgeModule",
                    "Data\\SkyrimTogetherReborn\\STRPluginMessagingBridge.dll"));
            !bridgeModule.empty()) {
            config.strBridgeModule = bridgeModule;
        }

        if (config.displayName == "Player") {
            config.displayName = GetComputerDisplayName();
        }

#if STRPM_ENABLE_UDP_BACKEND
        if (config.backendMode != STRPM::RuntimeBackendMode::kStrBridge) {
            for (int index = 1; index <= 8; ++index) {
                const auto key = "RemotePeer" + std::to_string(index);
                const auto value = ReadIniString(path, "Network", key.c_str(), "");
                const auto endpoint = ParseEndpoint(value, config.localPort);
                if (endpoint) {
                    config.remotePeers.push_back(*endpoint);
                }
            }
        }
#endif

        return config;
    }

    class Broker
    {
    public:
        static Broker& GetSingleton()
        {
            static Broker instance;
            return instance;
        }

        bool Start()
        {
            if (_running.load()) {
                return true;
            }

            _config = LoadConfig();
            {
                std::scoped_lock lock(_nameMutex);
                _displayName = _config.displayName;
            }
            {
                std::scoped_lock lock(_authMutex);
                _sharedSecret = _config.sharedSecret;
            }

            if (!_config.enabled) {
                return false;
            }

            CreateLocalIdentity();

            if (_config.backendMode != STRPM::RuntimeBackendMode::kUdp) {
                if (TryStartStrBridge()) {
                    _activeBackend.store(STRPM::RuntimeBackend::kStrBridge);
                    _running.store(true);
                    return true;
                }

                if (_config.backendMode == STRPM::RuntimeBackendMode::kStrBridge) {
                    return false;
                }
            }

#if !STRPM_ENABLE_UDP_BACKEND
            Log("UDP backend is disabled in this STR-only build");
            return false;
#else
            WSADATA wsa{};
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                return false;
            }
            _wsaStarted = true;

            _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (_socket == INVALID_SOCKET) {
                Stop();
                return false;
            }

            BOOL enabled = TRUE;
            setsockopt(
                _socket,
                SOL_SOCKET,
                SO_REUSEADDR,
                reinterpret_cast<const char*>(&enabled),
                sizeof(enabled));
            setsockopt(
                _socket,
                SOL_SOCKET,
                SO_BROADCAST,
                reinterpret_cast<const char*>(&enabled),
                sizeof(enabled));

            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            local.sin_port = htons(_config.localPort);
            if (bind(_socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) ==
                SOCKET_ERROR) {
                Stop();
                return false;
            }

            DWORD timeoutMs = 250;
            setsockopt(
                _socket,
                SOL_SOCKET,
                SO_RCVTIMEO,
                reinterpret_cast<const char*>(&timeoutMs),
                sizeof(timeoutMs));

            _broadcast = {};
            _broadcast.sin_family = AF_INET;
            _broadcast.sin_port = htons(_config.localPort);
            _broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);

            _activeBackend.store(STRPM::RuntimeBackend::kUdp);
            _running.store(true);
            _receiver = std::jthread([this](std::stop_token token) {
                ReceiverLoop(token);
            });
            _maintenance = std::jthread([this](std::stop_token token) {
                MaintenanceLoop(token);
            });

            SendHello();
            return true;
#endif
        }

        void Stop()
        {
            if (_running.exchange(false)) {
                if (_receiver.joinable()) {
                    _receiver.request_stop();
                }
                if (_maintenance.joinable()) {
                    _maintenance.request_stop();
                }
            }

            if (_strTransport != nullptr && _strTransport->stop != nullptr) {
                _strTransport->stop();
            }
            _strTransport = nullptr;
            _activeBackend.store(STRPM::RuntimeBackend::kNone);

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

            if (_wsaStarted) {
                WSACleanup();
                _wsaStarted = false;
            }
        }

        STRPM::Result RegisterChannel(
            const char* channel,
            STRPM::ReceiveCallback callback,
            void* userData,
            STRPM::ListenerHandle* outHandle)
        {
            if (!IsValidChannel(channel) ||
                callback == nullptr ||
                outHandle == nullptr) {
                return STRPM::Result::kInvalidArgument;
            }

            std::scoped_lock lock(_listenerMutex);
            const auto duplicate = std::ranges::any_of(
                _listeners,
                [channel](const Listener& listener) {
                    return listener.channel == channel;
                });
            if (duplicate) {
                return STRPM::Result::kChannelAlreadyRegistered;
            }

            Listener listener{};
            listener.handle.value = _nextHandle.fetch_add(1);
            listener.channel = channel;
            listener.callback = callback;
            listener.userData = userData;
            *outHandle = listener.handle;
            _listeners.push_back(listener);
            return STRPM::Result::kOk;
        }

        STRPM::Result UnregisterChannel(STRPM::ListenerHandle handle)
        {
            if (handle.value == 0) {
                return STRPM::Result::kInvalidArgument;
            }

            std::scoped_lock lock(_listenerMutex);
            const auto oldSize = _listeners.size();
            std::erase_if(_listeners, [handle](const Listener& listener) {
                return listener.handle.value == handle.value;
            });
            return oldSize == _listeners.size() ?
                STRPM::Result::kChannelNotRegistered :
                STRPM::Result::kOk;
        }

        STRPM::Result Send(
            const char* channel,
            STRPM::Target target,
            const void* data,
            std::size_t size,
            std::uint32_t flags)
        {
            if (!Start()) {
                return STRPM::Result::kNotConnected;
            }
            if (!IsValidChannel(channel) || (data == nullptr && size != 0)) {
                return STRPM::Result::kInvalidArgument;
            }
            if (size > STRPM::kMaxPayloadBytes) {
                return STRPM::Result::kPayloadTooLarge;
            }

            if (_activeBackend.load() == STRPM::RuntimeBackend::kStrBridge) {
                if (_strTransport == nullptr || _strTransport->send == nullptr) {
                    return STRPM::Result::kNotConnected;
                }
                return _strTransport->send(channel, target, data, size, flags);
            }

            const auto sequence = _nextSequence.fetch_add(1);
            const auto payload = std::string_view(
                static_cast<const char*>(data),
                size);
            auto packet =
                std::string(kDataPrefix) +
                "instance=" + _instanceID +
                "|id=" + std::to_string(_localConnectionID) +
                "|name=" + HexEncode(GetDisplayName()) +
                "|channel=" + HexEncode(channel) +
                "|seq=" + std::to_string(sequence) +
                "|flags=" + std::to_string(flags) +
                "|target=" + EncodeTarget(target) +
                "|relay=0" +
                "|payload=" + HexEncode(payload);
            packet = SignPacket(std::move(packet));

            if (packet.size() > kMaxDatagramBytes) {
                return STRPM::Result::kPayloadTooLarge;
            }

            bool delivered = false;
            if ((flags & STRPM::kMessageAllowLoopback) != 0) {
                Deliver(channel, payload, GetDisplayName(), _localConnectionID, flags, sequence);
                delivered = true;
            }

            const auto destinations = ResolveDestinations(target);
            if (destinations.empty()) {
                if (target.kind != STRPM::TargetKind::kAllPlayers) {
                    return delivered ? STRPM::Result::kOk : STRPM::Result::kTargetNotFound;
                }
                if (_config.autoDiscovery) {
                    const bool broadcastSent = SendPacketTo(packet, _broadcast);
                    if (_config.requireKnownPeer) {
                        Log("Send target=all has no known peers; broadcast fallback sent only");
                        return delivered ? STRPM::Result::kOk : STRPM::Result::kTargetNotFound;
                    }
                    return broadcastSent || delivered ?
                        STRPM::Result::kOk :
                        STRPM::Result::kTransportError;
                }
                return delivered ? STRPM::Result::kOk : STRPM::Result::kTargetNotFound;
            }

            bool sent = false;
            for (const auto& destination : destinations) {
                sent = SendPacketTo(packet, destination) || sent;
            }

            return sent || delivered ? STRPM::Result::kOk : STRPM::Result::kTransportError;
        }

        STRPM::ConnectionID GetLocalConnectionID() const
        {
            if (_activeBackend.load() == STRPM::RuntimeBackend::kStrBridge &&
                _strTransport != nullptr &&
                _strTransport->getLocalConnectionID != nullptr) {
                STRPM::ConnectionID connectionID = 0;
                if (_strTransport->getLocalConnectionID(&connectionID) ==
                        STRPM::Result::kOk &&
                    connectionID != 0) {
                    return connectionID;
                }
            }
            return _localConnectionID;
        }

        STRPM::RuntimeStatus GetRuntimeStatus() const
        {
            STRPM::RuntimeStatus status{};
            status.knownPeerCount = KnownPeerCount();
            status.configuredPeerCount =
                static_cast<std::uint32_t>(_config.remotePeers.size());
            status.autoDiscovery = _config.autoDiscovery ? 1 : 0;
            status.relayMode = _config.relayMode ? 1 : 0;
            status.requireKnownPeer = _config.requireKnownPeer ? 1 : 0;
            status.localPort = _config.localPort;
            status.activeBackend = _activeBackend.load();
            status.configuredBackendMode = _config.backendMode;
            status.strBridgeAvailable = _strBridgeAvailable.load() ? 1 : 0;
            status.strBridgeActive =
                _activeBackend.load() == STRPM::RuntimeBackend::kStrBridge ? 1 : 0;
            return status;
        }

        void SetLogCallback(STRPM::LogCallback callback, void* userData)
        {
            std::scoped_lock lock(_logMutex);
            _logCallback = callback;
            _logUserData = userData;
        }

        void SetLocalDisplayName(const char* displayName)
        {
            std::scoped_lock lock(_nameMutex);
            if (displayName != nullptr && displayName[0] != '\0') {
                _displayName = SanitizeDisplayName(displayName);
                if (_activeBackend.load() == STRPM::RuntimeBackend::kStrBridge &&
                    _strTransport != nullptr &&
                    _strTransport->setLocalDisplayName != nullptr) {
                    _strTransport->setLocalDisplayName(_displayName.c_str());
                }
            }
        }

    private:
        Broker() = default;
        ~Broker()
        {
            Stop();
        }

        void CreateLocalIdentity()
        {
            LARGE_INTEGER counter{};
            QueryPerformanceCounter(&counter);
            _instanceID = HexEncode(
                GetDisplayName() + ":" +
                std::to_string(GetCurrentProcessId()) + ":" +
                std::to_string(counter.QuadPart));
            _localConnectionID = HashText(_instanceID);
            _nextSequence.store(1);
        }

        static HMODULE LoadStrBridgeModule(const std::wstring& modulePath)
        {
            if (modulePath.empty()) {
                return nullptr;
            }

            if (auto module = GetModuleHandleW(modulePath.c_str());
                module != nullptr) {
                return module;
            }

            const auto separator = modulePath.find_last_of(L"\\/");
            const auto moduleName = separator == std::wstring::npos ?
                modulePath :
                modulePath.substr(separator + 1);
            if (!moduleName.empty()) {
                if (auto module = GetModuleHandleW(moduleName.c_str());
                    module != nullptr) {
                    return module;
                }
            }

            if (auto module = LoadLibraryW(modulePath.c_str());
                module != nullptr) {
                return module;
            }

            if (!moduleName.empty() && moduleName != modulePath) {
                if (auto module = LoadLibraryW(moduleName.c_str());
                    module != nullptr) {
                    return module;
                }
            }

            if (!moduleName.empty()) {
                std::wstring defaultPath = L"Data\\SkyrimTogetherReborn\\";
                defaultPath += moduleName;
                if (defaultPath != modulePath) {
                    return LoadLibraryW(defaultPath.c_str());
                }
            }

            return nullptr;
        }

        bool TryStartStrBridge()
        {
            _strBridgeAvailable.store(false);

            const auto module = LoadStrBridgeModule(_config.strBridgeModule);
            if (module == nullptr) {
                return false;
            }

            const auto rawExport =
                GetProcAddress(module, STRPM::kQueryTransportExportName);
            if (rawExport == nullptr) {
                return false;
            }

            const auto queryTransport =
                reinterpret_cast<STRPM::QueryTransportInterfaceFn>(rawExport);
            const STRPM::TransportInterface* transport = nullptr;
            const auto queryResult = queryTransport(
                STRPM::kTransportInterfaceVersion,
                &transport);
            if (queryResult != STRPM::Result::kOk ||
                transport == nullptr ||
                transport->version != STRPM::kTransportInterfaceVersion ||
                transport->start == nullptr ||
                transport->stop == nullptr ||
                transport->send == nullptr) {
                return false;
            }

            _strBridgeAvailable.store(true);

            const auto startResult = transport->start(&StrBridgeReceiveThunk, this);
            if (startResult != STRPM::Result::kOk) {
                return false;
            }

            _strModule = module;
            _strTransport = transport;

            if (_strTransport->setLocalDisplayName != nullptr) {
                const auto displayName = GetDisplayName();
                _strTransport->setLocalDisplayName(displayName.c_str());
            }

            if (_strTransport->getLocalConnectionID != nullptr) {
                STRPM::ConnectionID connectionID = 0;
                if (_strTransport->getLocalConnectionID(&connectionID) ==
                        STRPM::Result::kOk &&
                    connectionID != 0) {
                    _localConnectionID = connectionID;
                }
            }

            Log("STR bridge transport active");
            return true;
        }

        static void STRPM_CALL StrBridgeReceiveThunk(
            const STRPM::Message* message,
            void* userData)
        {
            if (message == nullptr || userData == nullptr) {
                return;
            }

            static_cast<Broker*>(userData)->DeliverFromStrBridge(*message);
        }

        void DeliverFromStrBridge(const STRPM::Message& message)
        {
            if (!IsValidChannel(message.channel) ||
                (message.data == nullptr && message.size != 0) ||
                message.size > STRPM::kMaxPayloadBytes) {
                return;
            }

            const std::string_view payload(
                static_cast<const char*>(message.data),
                message.size);
            const auto senderName = message.sender.displayName != nullptr ?
                std::string_view(message.sender.displayName) :
                std::string_view("STR");

            Deliver(
                message.channel,
                payload,
                senderName,
                message.sender.connectionID,
                message.flags,
                message.sequence);
        }

        static bool IsValidChannel(const char* channel)
        {
            if (channel == nullptr || channel[0] == '\0') {
                return false;
            }
            const auto length = std::strlen(channel);
            if (length > STRPM::kMaxChannelLength) {
                return false;
            }
            for (std::size_t i = 0; i < length; ++i) {
                const auto c = static_cast<unsigned char>(channel[i]);
                const bool valid =
                    (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                    c == '.' ||
                    c == '_' ||
                    c == '-';
                if (!valid) {
                    return false;
                }
            }
            return true;
        }

        static STRPM::ConnectionID HashText(std::string_view text)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (const auto c : text) {
                hash ^= static_cast<unsigned char>(c);
                hash *= 1099511628211ull;
            }
            return hash == 0 ? 1 : hash;
        }

        std::string GetDisplayName() const
        {
            std::scoped_lock lock(_nameMutex);
            return _displayName.empty() ? "Player" : _displayName;
        }

        std::string GetSharedSecret() const
        {
            std::scoped_lock lock(_authMutex);
            return _sharedSecret;
        }

        std::string SignPacket(std::string packet) const
        {
            packet = RemoveAuthField(packet);
            const auto secret = GetSharedSecret();
            if (secret.empty()) {
                return packet;
            }

            const auto tag = ComputeHmacSha256(secret, packet);
            if (!tag) {
                return {};
            }
            return packet + "|auth=" + *tag;
        }

        bool AuthenticatePacket(std::string_view packet) const
        {
            const auto secret = GetSharedSecret();
            if (secret.empty()) {
                return true;
            }

            const auto authPos = packet.rfind(kAuthField);
            if (authPos == std::string_view::npos) {
                return false;
            }

            const auto supplied = packet.substr(authPos + kAuthField.size());
            const auto unsignedPacket = RemoveAuthField(packet);
            const auto expected = ComputeHmacSha256(secret, unsignedPacket);
            if (!expected || supplied.size() != expected->size()) {
                return false;
            }

            unsigned char difference = 0;
            for (std::size_t i = 0; i < supplied.size(); ++i) {
                difference |= static_cast<unsigned char>(supplied[i] ^ (*expected)[i]);
            }
            return difference == 0;
        }

        std::string EncodeTarget(STRPM::Target target) const
        {
            if (target.kind == STRPM::TargetKind::kAllPlayers) {
                return "all";
            }
            if (target.kind == STRPM::TargetKind::kHost) {
                return "host";
            }
            if (target.kind == STRPM::TargetKind::kServer) {
                return "server";
            }
            if (target.connectionID != 0) {
                return "id:" + std::to_string(target.connectionID);
            }
            if (target.displayName != nullptr && target.displayName[0] != '\0') {
                return "name:" + HexEncode(SanitizeDisplayName(target.displayName));
            }
            return "player";
        }

        bool TargetMatchesLocal(std::string_view target) const
        {
            if (target == "all") {
                return true;
            }
            if ((target == "host" || target == "server") && _config.relayMode) {
                return true;
            }
            if (target.starts_with("id:")) {
                return ParseUInt64(target.substr(3)) == _localConnectionID;
            }
            if (target.starts_with("name:")) {
                const auto decoded = HexDecode(target.substr(5));
                return decoded && EqualsInsensitive(*decoded, GetDisplayName());
            }
            return false;
        }

        std::vector<sockaddr_in> ResolveDestinations(STRPM::Target target) const
        {
            std::vector<sockaddr_in> result;
            std::unordered_set<std::string> seen;

            const auto add = [&](const sockaddr_in& address) {
                const auto endpoint = AddressToString(address);
                if (seen.insert(endpoint).second) {
                    result.push_back(address);
                }
            };

            if (target.kind == STRPM::TargetKind::kAllPlayers ||
                target.kind == STRPM::TargetKind::kHost ||
                target.kind == STRPM::TargetKind::kServer) {
                for (const auto& peer : _config.remotePeers) {
                    add(peer);
                }
            }

            std::scoped_lock lock(_peerMutex);
            for (const auto& peer : _peers) {
                bool matches = target.kind == STRPM::TargetKind::kAllPlayers;
                if (target.kind == STRPM::TargetKind::kPlayer) {
                    matches =
                        (target.connectionID != 0 &&
                            peer.connectionID == target.connectionID) ||
                        (target.displayName != nullptr &&
                            EqualsInsensitive(peer.displayName, target.displayName));
                }

                if (matches) {
                    add(peer.address);
                }
            }

            return result;
        }

        std::uint32_t KnownPeerCount() const
        {
            std::scoped_lock lock(_peerMutex);
            return static_cast<std::uint32_t>(_peers.size());
        }

        bool SendPacketTo(std::string_view packet, const sockaddr_in& destination) const
        {
            if (_socket == INVALID_SOCKET || packet.empty()) {
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
            return sent != SOCKET_ERROR;
        }

        void SendHello()
        {
            if (!_running.load() || _socket == INVALID_SOCKET) {
                return;
            }

            const auto sendTo = [&](const sockaddr_in& destination, bool observed) {
                auto packet =
                    std::string(kDiscoveryPrefix) +
                    "instance=" + _instanceID +
                    "|id=" + std::to_string(_localConnectionID) +
                    "|name=" + HexEncode(GetDisplayName()) +
                    "|port=" + std::to_string(_config.localPort) +
                    "|observed=" + (observed ? "1" : "0");
                packet = SignPacket(std::move(packet));
                SendPacketTo(packet, destination);
            };

            if (_config.autoDiscovery) {
                sendTo(_broadcast, false);
            }
            for (const auto& peer : _config.remotePeers) {
                sendTo(peer, true);
            }
        }

        void RegisterPeer(
            const sockaddr_in& source,
            std::uint16_t advertisedPort,
            bool useObservedPort,
            STRPM::ConnectionID connectionID,
            std::string instanceID,
            std::string displayName)
        {
            if (instanceID.empty() || instanceID == _instanceID || connectionID == 0) {
                return;
            }

            sockaddr_in address = source;
            if (!useObservedPort) {
                address.sin_port = htons(advertisedPort);
            }

            std::scoped_lock lock(_peerMutex);
            const auto endpoint = AddressToString(address);
            const auto it = std::ranges::find_if(_peers, [&](const Peer& peer) {
                return peer.instanceID == instanceID || SameEndpoint(peer.address, address);
            });

            if (it == _peers.end()) {
                _peers.push_back(Peer{
                    address,
                    connectionID,
                    std::move(instanceID),
                    std::move(displayName),
                    std::chrono::steady_clock::now()
                });
                Log(("Peer discovered: " + endpoint).c_str());
                return;
            }

            it->address = address;
            it->connectionID = connectionID;
            it->displayName = std::move(displayName);
            it->lastSeen = std::chrono::steady_clock::now();
        }

        void HandleHello(std::string_view packet, const sockaddr_in& source)
        {
            const auto instance = ReadField(packet, "instance");
            const auto id = ReadField(packet, "id");
            const auto name = ReadField(packet, "name");
            const auto port = ReadField(packet, "port");
            const auto observed = ReadField(packet, "observed");
            if (!instance || !id || !name || !port) {
                return;
            }

            const auto decodedName = HexDecode(*name);
            const auto advertisedPort = static_cast<std::uint16_t>(ParseUInt32(*port));
            if (!decodedName || advertisedPort == 0) {
                return;
            }

            RegisterPeer(
                source,
                advertisedPort,
                observed && *observed == "1",
                ParseUInt64(*id),
                *instance,
                SanitizeDisplayName(*decodedName));
        }

        void HandleData(std::string_view packet, const sockaddr_in& source)
        {
            const auto instance = ReadField(packet, "instance");
            const auto id = ReadField(packet, "id");
            const auto name = ReadField(packet, "name");
            const auto channel = ReadField(packet, "channel");
            const auto payload = ReadField(packet, "payload");
            const auto flags = ReadField(packet, "flags");
            const auto sequence = ReadField(packet, "seq");
            const auto target = ReadField(packet, "target");
            if (!instance || !id || !name || !channel || !payload || !target) {
                return;
            }
            if (*instance == _instanceID) {
                return;
            }

            const auto decodedName = HexDecode(*name);
            const auto decodedChannel = HexDecode(*channel);
            const auto decodedPayload = HexDecode(*payload);
            if (!decodedName || !decodedChannel || !decodedPayload) {
                return;
            }

            RegisterPeer(
                source,
                ntohs(source.sin_port),
                true,
                ParseUInt64(*id),
                *instance,
                SanitizeDisplayName(*decodedName));

            RelayPacket(packet, source);
            if (!TargetMatchesLocal(*target)) {
                return;
            }

            Deliver(
                *decodedChannel,
                *decodedPayload,
                *decodedName,
                ParseUInt64(*id),
                flags ? ParseUInt32(*flags) : STRPM::kMessageNone,
                sequence ? ParseUInt64(*sequence) : 0);
        }

        void RelayPacket(std::string_view packet, const sockaddr_in& source)
        {
            if (!_config.relayMode) {
                return;
            }

            const auto relay = ReadField(packet, "relay");
            if (relay && *relay != "0") {
                return;
            }

            auto relayed = RemoveAuthField(packet);
            const auto relayPos = relayed.find("|relay=0|");
            if (relayPos != std::string::npos) {
                relayed.replace(relayPos, 9, "|relay=1|");
            } else {
                relayed += "|relay=1";
            }
            relayed = SignPacket(std::move(relayed));

            const auto destinations = ResolveDestinations(STRPM::Target{
                STRPM::TargetKind::kAllPlayers,
                0,
                nullptr
            });
            for (const auto& destination : destinations) {
                if (!SameEndpoint(destination, source)) {
                    SendPacketTo(relayed, destination);
                }
            }
        }

        void Deliver(
            std::string_view channel,
            std::string_view payload,
            std::string_view senderName,
            STRPM::ConnectionID senderID,
            std::uint32_t flags,
            std::uint64_t sequence)
        {
            std::vector<Listener> listeners;
            {
                std::scoped_lock lock(_listenerMutex);
                for (const auto& listener : _listeners) {
                    if (listener.channel == channel) {
                        listeners.push_back(listener);
                    }
                }
            }

            const std::string channelCopy(channel);
            const std::string payloadCopy(payload);
            const std::string senderCopy(senderName);
            for (const auto& listener : listeners) {
                const STRPM::Message message{
                    channelCopy.c_str(),
                    payloadCopy.data(),
                    payloadCopy.size(),
                    STRPM::Sender{ senderID, senderCopy.c_str(), false },
                    flags,
                    sequence
                };
                listener.callback(&message, listener.userData);
            }
        }

        void ExpirePeers()
        {
            const auto now = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds(_config.peerTimeoutMs);

            std::scoped_lock lock(_peerMutex);
            std::erase_if(_peers, [&](const Peer& peer) {
                return now - peer.lastSeen > timeout;
            });
        }

        void MaintenanceLoop(std::stop_token token)
        {
            while (!token.stop_requested() && _running.load()) {
                SendHello();
                ExpirePeers();

                const auto delay = std::chrono::milliseconds(_config.discoveryIntervalMs);
                const auto slice = std::chrono::milliseconds(100);
                auto slept = std::chrono::milliseconds(0);
                while (slept < delay && !token.stop_requested() && _running.load()) {
                    std::this_thread::sleep_for(slice);
                    slept += slice;
                }
            }
        }

        void ReceiverLoop(std::stop_token token)
        {
            std::array<char, 65536> buffer{};
            while (!token.stop_requested() && _running.load()) {
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
                    if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                        continue;
                    }
                    if (_running.load()) {
                        Log("UDP receive failed");
                    }
                    continue;
                }
                if (received <= 0) {
                    continue;
                }

                const std::string packet(buffer.data(), static_cast<std::size_t>(received));
                if (!AuthenticatePacket(packet)) {
                    Log("Dropped unauthenticated packet");
                    continue;
                }
                if (packet.starts_with(kDiscoveryPrefix)) {
                    HandleHello(packet, source);
                } else if (packet.starts_with(kDataPrefix)) {
                    HandleData(packet, source);
                }
            }
        }

        void Log(const char* message) const
        {
            std::scoped_lock lock(_logMutex);
            if (_logCallback != nullptr) {
                _logCallback(message, _logUserData);
            }
        }

        Config _config{};
        std::atomic<STRPM::RuntimeBackend> _activeBackend{
            STRPM::RuntimeBackend::kNone
        };

        HMODULE _strModule{ nullptr };
        const STRPM::TransportInterface* _strTransport{ nullptr };
        std::atomic_bool _strBridgeAvailable{ false };

        SOCKET _socket{ INVALID_SOCKET };
        sockaddr_in _broadcast{};
        bool _wsaStarted{ false };

        std::jthread _receiver;
        std::jthread _maintenance;
        std::atomic_bool _running{ false };

        std::string _instanceID;
        STRPM::ConnectionID _localConnectionID{ 0 };
        std::atomic_uint64_t _nextSequence{ 1 };

        mutable std::mutex _sendMutex;
        mutable std::mutex _peerMutex;
        std::vector<Peer> _peers;

        mutable std::mutex _listenerMutex;
        std::vector<Listener> _listeners;
        std::atomic_uint64_t _nextHandle{ 1 };

        mutable std::mutex _nameMutex;
        std::string _displayName;

        mutable std::mutex _authMutex;
        std::string _sharedSecret;

        mutable std::mutex _logMutex;
        STRPM::LogCallback _logCallback{ nullptr };
        void* _logUserData{ nullptr };
    };

    STRPM::Result STRPM_CALL RegisterChannel(
        const char* channel,
        STRPM::ReceiveCallback callback,
        void* userData,
        STRPM::ListenerHandle* outHandle)
    {
        return Broker::GetSingleton().RegisterChannel(
            channel,
            callback,
            userData,
            outHandle);
    }

    STRPM::Result STRPM_CALL UnregisterChannel(STRPM::ListenerHandle handle)
    {
        return Broker::GetSingleton().UnregisterChannel(handle);
    }

    STRPM::Result STRPM_CALL Send(
        const char* channel,
        STRPM::Target target,
        const void* data,
        std::size_t size,
        std::uint32_t flags)
    {
        return Broker::GetSingleton().Send(channel, target, data, size, flags);
    }

    STRPM::Result STRPM_CALL GetLocalConnectionID(
        STRPM::ConnectionID* outConnectionID)
    {
        if (outConnectionID == nullptr) {
            return STRPM::Result::kInvalidArgument;
        }

        Broker::GetSingleton().Start();
        *outConnectionID = Broker::GetSingleton().GetLocalConnectionID();
        return STRPM::Result::kOk;
    }

    STRPM::Result STRPM_CALL GetRuntimeStatus(
        STRPM::RuntimeStatus* outStatus)
    {
        if (outStatus == nullptr) {
            return STRPM::Result::kInvalidArgument;
        }

        Broker::GetSingleton().Start();
        *outStatus = Broker::GetSingleton().GetRuntimeStatus();
        return STRPM::Result::kOk;
    }

    STRPM::Result STRPM_CALL SetLogCallback(
        STRPM::LogCallback callback,
        void* userData)
    {
        Broker::GetSingleton().SetLogCallback(callback, userData);
        return STRPM::Result::kOk;
    }

    STRPM::Result STRPM_CALL SetLocalDisplayName(const char* displayName)
    {
        if (displayName == nullptr || displayName[0] == '\0') {
            return STRPM::Result::kInvalidArgument;
        }

        Broker::GetSingleton().SetLocalDisplayName(displayName);
        return STRPM::Result::kOk;
    }

    const STRPM::Interface g_interface{
        STRPM::kInterfaceVersion,
        &RegisterChannel,
        &UnregisterChannel,
        &Send,
        &GetLocalConnectionID,
        &SetLogCallback,
        &SetLocalDisplayName
    };

    const STRPM::DiagnosticsInterface g_diagnostics{
        STRPM::kDiagnosticsVersion,
        &GetRuntimeStatus
    };
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(
    const SKSEInterface*,
    PluginInfo* pluginInfo)
{
    if (pluginInfo == nullptr) {
        return false;
    }

    pluginInfo->infoVersion = 1;
    pluginInfo->name = "STRPluginMessagingAPI";
    pluginInfo->version = 5;
    return true;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface*)
{
    Broker::GetSingleton().Start();
    return true;
}

STRPM_EXPORT STRPM::Result STRPM_CALL STR_QueryPluginMessagingInterface(
    std::uint32_t requestedVersion,
    const STRPM::Interface** outInterface)
{
    if (outInterface == nullptr) {
        return STRPM::Result::kInvalidArgument;
    }

    *outInterface = nullptr;
    if (requestedVersion != STRPM::kInterfaceVersion) {
        return STRPM::Result::kUnsupportedVersion;
    }

    Broker::GetSingleton().Start();
    *outInterface = &g_interface;
    return STRPM::Result::kOk;
}

STRPM_EXPORT STRPM::Result STRPM_CALL STR_QueryPluginMessagingDiagnostics(
    std::uint32_t requestedVersion,
    const STRPM::DiagnosticsInterface** outInterface)
{
    if (outInterface == nullptr) {
        return STRPM::Result::kInvalidArgument;
    }

    *outInterface = nullptr;
    if (requestedVersion != STRPM::kDiagnosticsVersion) {
        return STRPM::Result::kUnsupportedVersion;
    }

    Broker::GetSingleton().Start();
    *outInterface = &g_diagnostics;
    return STRPM::Result::kOk;
}
