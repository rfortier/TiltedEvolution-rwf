#pragma once

#include "PCH.h"
#include "Config.h"

namespace IEDSyncTogether::StrServerDiscovery
{
    struct ClientState
    {
        std::optional<Config::RemotePeer> remotePeer;
        std::optional<std::string> password;
        std::string rawAddress;
    };

    ClientState ReadClientState(std::uint16_t iedSyncPort);
    std::optional<std::string> ReadServerPasswordFromConfig();
}
