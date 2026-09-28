#pragma once

#include "STRPMTransport.h"

namespace OStimTogether
{
    // Temporary source-compatibility alias for AddonBridge.cpp.
    // There is no UDP backend or fallback on the strpm branch.
    using UdpTransport = STRPMTransport;
}
