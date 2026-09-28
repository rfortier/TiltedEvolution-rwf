#pragma once

#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace STRPMBridgeReceive
{
    bool Start(STRPM::ReceiveCallback callback, void* userData) noexcept;
    void Stop() noexcept;
    bool IsResolved() noexcept;
}
