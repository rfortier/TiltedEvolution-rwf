#pragma once

#include "StrMessagingTransport.h"

namespace TradeTogether
{
    // Compatibility alias for the current trade core. The strpm branch does
    // not create a UDP socket: every call is routed through STR Plugin Messaging.
    using UdpTransport = StrMessagingTransport;
}
