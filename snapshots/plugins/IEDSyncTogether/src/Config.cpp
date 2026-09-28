#include "PCH.h"
#include "Config.h"

namespace IEDSyncTogether
{
    namespace
    {
        constexpr auto kIniPath = L"Data\\SKSE\\Plugins\\IEDSyncTogether.ini";
        constexpr auto kRelayHostIniPath =
            L"Data\\SKSE\\Plugins\\IEDSyncTogether_RelayHost.ini";

        std::uint32_t ReadUInt(
            const wchar_t* section,
            const wchar_t* key,
            std::uint32_t fallback,
            const wchar_t* path = kIniPath)
        {
            return static_cast<std::uint32_t>(GetPrivateProfileIntW(
                section,
                key,
                static_cast<int>(fallback),
                path));
        }

        bool ReadBool(
            const wchar_t* section,
            const wchar_t* key,
            bool fallback,
            const wchar_t* path = kIniPath)
        {
            return ReadUInt(section, key, fallback ? 1U : 0U, path) != 0;
        }

        std::string ReadString(
            const wchar_t* section,
            const wchar_t* key,
            const wchar_t* fallback,
            const wchar_t* path = kIniPath)
        {
            std::array<wchar_t, 2048> buffer{};
            GetPrivateProfileStringW(
                section,
                key,
                fallback,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                path);

            if (buffer[0] == L'\0') {
                return {};
            }

            const int needed = WideCharToMultiByte(
                CP_UTF8,
                0,
                buffer.data(),
                -1,
                nullptr,
                0,
                nullptr,
                nullptr);
            if (needed <= 1) {
                return {};
            }

            std::string value(static_cast<std::size_t>(needed), '\0');
            const int written = WideCharToMultiByte(
                CP_UTF8,
                0,
                buffer.data(),
                -1,
                value.data(),
                needed,
                nullptr,
                nullptr);
            if (written <= 1) {
                return {};
            }

            value.resize(static_cast<std::size_t>(written - 1));
            return value;
        }

        std::uint32_t Clamp(
            std::uint32_t value,
            std::uint32_t minValue,
            std::uint32_t maxValue)
        {
            return std::max(minValue, std::min(value, maxValue));
        }

        std::uint16_t ClampPort(std::uint32_t value, std::uint16_t fallback)
        {
            return value > 0 && value <= 65535 ?
                static_cast<std::uint16_t>(value) : fallback;
        }

        std::string Trim(std::string value)
        {
            const auto isSpace = [](unsigned char ch) {
                return std::isspace(ch) != 0;
            };
            value.erase(
                value.begin(),
                std::find_if_not(value.begin(), value.end(), isSpace));
            value.erase(
                std::find_if_not(value.rbegin(), value.rend(), isSpace).base(),
                value.end());
            return value;
        }

        Config::TransportMode ParseTransportMode(std::string value)
        {
            value = Trim(std::move(value));
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (value == "auto") {
                return Config::TransportMode::kAuto;
            }
            if (value == "udp" || value == "legacyudp") {
                return Config::TransportMode::kUDP;
            }
            return Config::TransportMode::kSTR;
        }

        std::optional<Config::RemotePeer> ParseRemotePeer(
            std::string value,
            std::uint16_t defaultPort)
        {
            value = Trim(std::move(value));
            if (value.empty()) {
                return std::nullopt;
            }

            Config::RemotePeer peer{};
            peer.port = defaultPort;

            const auto separator = value.rfind(':');
            if (separator != std::string::npos) {
                const auto portText = Trim(value.substr(separator + 1));
                try {
                    const auto parsed = std::stoul(portText);
                    if (parsed == 0 || parsed > 65535) {
                        return std::nullopt;
                    }
                    peer.port = static_cast<std::uint16_t>(parsed);
                    value.resize(separator);
                } catch (...) {
                    return std::nullopt;
                }
            }

            peer.host = Trim(std::move(value));
            if (peer.host.empty() || peer.host.find('|') != std::string::npos) {
                return std::nullopt;
            }
            return peer;
        }

        std::vector<Config::RemotePeer> ParseRemotePeers(
            std::string value,
            std::uint16_t defaultPort)
        {
            std::vector<Config::RemotePeer> peers;
            std::unordered_set<std::string> seen;

            std::size_t start = 0;
            while (start <= value.size()) {
                const auto end = value.find_first_of(",;", start);
                const auto item = value.substr(
                    start,
                    end == std::string::npos ? std::string::npos : end - start);

                if (auto peer = ParseRemotePeer(item, defaultPort)) {
                    auto key = peer->host;
                    std::transform(
                        key.begin(),
                        key.end(),
                        key.begin(),
                        [](unsigned char ch) {
                            return static_cast<char>(std::tolower(ch));
                        });
                    key = fmt::format("{}:{}", key, peer->port);
                    if (seen.insert(key).second) {
                        peers.push_back(std::move(*peer));
                    }
                } else if (!Trim(item).empty()) {
                    SKSE::log::warn(
                        "IEDSTNET ignored invalid RemotePeers entry: \"{}\"",
                        Trim(item));
                }

                if (peers.size() >= 64) {
                    SKSE::log::warn("IEDSTNET RemotePeers limited to 64 entries");
                    break;
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }

            return peers;
        }
    }

    Config Config::Load()
    {
        Config config{};
        config.networkEnabled =
            ReadBool(L"Network", L"Enabled", config.networkEnabled) &&
            !ReadBool(L"Network", L"Disabled", false);
        config.transportMode = ParseTransportMode(
            ReadString(L"Network", L"Transport", L"STR"));
        config.udpFallback = ReadBool(L"Network", L"UdpFallback", config.udpFallback);
        config.requireStrBridge =
            ReadBool(L"Network", L"RequireStrBridge", config.requireStrBridge);
        config.autoDiscovery = ReadBool(L"Network", L"AutoDiscovery", config.autoDiscovery);
        config.relayMode = ReadBool(L"Network", L"RelayMode", config.relayMode);
        config.autoRemoteFromSTR = ReadBool(
            L"Network",
            L"AutoRemoteFromSTR",
            config.autoRemoteFromSTR);
        config.autoSharedSecretFromSTR = ReadBool(
            L"Network",
            L"AutoSharedSecretFromSTR",
            config.autoSharedSecretFromSTR);
        config.suppressRemoteNpcDisplays = ReadBool(
            L"Compatibility",
            L"SuppressRemoteNpcDisplays",
            config.suppressRemoteNpcDisplays);
        config.localPort = ClampPort(
            ReadUInt(L"Network", L"LocalPort", config.localPort),
            config.localPort);
        config.peerPort = ClampPort(
            ReadUInt(L"Network", L"PeerPort", config.localPort),
            config.localPort);
        config.autoRemotePort = ClampPort(
            ReadUInt(L"Network", L"AutoRemotePort", config.localPort),
            config.localPort);
        config.captureIntervalMs = Clamp(
            ReadUInt(L"Timing", L"CaptureIntervalMs", config.captureIntervalMs),
            250U,
            30000U);
        config.resendIntervalMs = Clamp(
            ReadUInt(L"Timing", L"ResendIntervalMs", config.resendIntervalMs),
            1000U,
            60000U);
        config.discoveryIntervalMs = Clamp(
            ReadUInt(L"Network", L"DiscoveryIntervalMs", config.discoveryIntervalMs),
            250U,
            30000U);
        config.peerTimeoutMs = Clamp(
            ReadUInt(L"Network", L"PeerTimeoutMs", config.peerTimeoutMs),
            2000U,
            120000U);
        config.peerHost = ReadString(L"Network", L"PeerHost", L"");
        config.remotePeers = ParseRemotePeers(
            ReadString(L"Network", L"RemotePeers", L""),
            config.peerPort);
        if (auto legacyPeer = ParseRemotePeer(config.peerHost, config.peerPort)) {
            const auto duplicate = std::ranges::any_of(
                config.remotePeers,
                [&](const Config::RemotePeer& peer) {
                    return _stricmp(peer.host.c_str(), legacyPeer->host.c_str()) == 0 &&
                           peer.port == legacyPeer->port;
                });
            if (!duplicate) {
                config.remotePeers.push_back(std::move(*legacyPeer));
            }
        }
        config.sharedSecret = ReadString(L"Network", L"SharedSecret", L"");

        if (GetFileAttributesW(kRelayHostIniPath) != INVALID_FILE_ATTRIBUTES) {
            const auto relayTransport = Trim(ReadString(
                L"Network",
                L"Transport",
                L"",
                kRelayHostIniPath));
            if (!relayTransport.empty()) {
                config.transportMode = ParseTransportMode(relayTransport);
            }
            config.udpFallback = ReadBool(
                L"Network",
                L"UdpFallback",
                config.udpFallback,
                kRelayHostIniPath);
            config.requireStrBridge = ReadBool(
                L"Network",
                L"RequireStrBridge",
                config.requireStrBridge,
                kRelayHostIniPath);
            config.autoDiscovery = ReadBool(
                L"Network",
                L"AutoDiscovery",
                config.autoDiscovery,
                kRelayHostIniPath);
            config.relayMode = ReadBool(
                L"Network",
                L"RelayMode",
                config.relayMode,
                kRelayHostIniPath);
            config.autoRemoteFromSTR = ReadBool(
                L"Network",
                L"AutoRemoteFromSTR",
                config.autoRemoteFromSTR,
                kRelayHostIniPath);
            config.autoSharedSecretFromSTR = ReadBool(
                L"Network",
                L"AutoSharedSecretFromSTR",
                config.autoSharedSecretFromSTR,
                kRelayHostIniPath);

            config.localPort = ClampPort(
                ReadUInt(L"Network", L"LocalPort", config.localPort, kRelayHostIniPath),
                config.localPort);
            config.autoRemotePort = ClampPort(
                ReadUInt(
                    L"Network",
                    L"AutoRemotePort",
                    config.autoRemotePort,
                    kRelayHostIniPath),
                config.autoRemotePort);

            const auto relaySharedSecret = ReadString(
                L"Network",
                L"SharedSecret",
                L"",
                kRelayHostIniPath);
            if (!relaySharedSecret.empty()) {
                config.sharedSecret = relaySharedSecret;
            }
        }

        return config;
    }
}
