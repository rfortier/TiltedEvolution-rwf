#pragma once

#include "PCH.h"

namespace IEDSyncTogether
{
    bool EqualsInsensitive(std::string_view left, std::string_view right);
    bool IsLikelyRemotePlayerProxy(RE::Actor* actor);
    std::vector<RE::Actor*> FindRemotePlayerProxies();
    RE::Actor* FindRemotePlayerProxy(std::string_view playerName);
}
