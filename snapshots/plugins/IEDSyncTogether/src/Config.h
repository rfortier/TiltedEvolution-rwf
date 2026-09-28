#pragma once

#include "PCH.h"

namespace IEDSyncTogether
{
    struct Config
    {
        enum class TransportMode : std::uint32_t
        {
            kSTR = 0,
            kAuto = 1,
            kUDP = 2
        };

        struct RemotePeer
        {
            std::string host;
            std::uint16_t port{ 38471 };
        };

        bool networkEnabled{ true };
        TransportMode transportMode{ TransportMode::kSTR };
        bool udpFallback{ false };
        bool requireStrBridge{ true };
        bool autoDiscovery{ true };
        bool relayMode{ false };
        bool autoRemoteFromSTR{ true };
        bool autoSharedSecretFromSTR{ false };
        bool suppressRemoteNpcDisplays{ false };
        std::uint16_t localPort{ 38471 };
        std::uint16_t peerPort{ 38471 };
        std::uint16_t autoRemotePort{ 38471 };
        std::uint32_t captureIntervalMs{ 750 };
        std::uint32_t resendIntervalMs{ 5000 };
        std::uint32_t discoveryIntervalMs{ 2000 };
        std::uint32_t peerTimeoutMs{ 10000 };
        std::string peerHost;
        std::vector<RemotePeer> remotePeers;
        std::string sharedSecret;

        static Config Load();
    };
}
