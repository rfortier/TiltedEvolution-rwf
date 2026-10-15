#pragma once

#include <type_traits>

/**
 * @brief Dispatched every tick.
 */
struct UpdateEvent
{
    double Delta;
};

// GameServer::OnUpdate brace-initialises this event straight from
// WireLimits::ClampFrameDelta, and the client's twin event carries a double too.
// A float member silently turns that construction into a narrowing conversion.
static_assert(std::is_same_v<decltype(UpdateEvent::Delta), double>,
    "Delta must stay as wide as WireLimits::ClampFrameDelta returns: GameServer::OnUpdate brace-initialises this event "
    "from that value, and narrowing it back to float is an MSVC error (C2397), not a warning.");
