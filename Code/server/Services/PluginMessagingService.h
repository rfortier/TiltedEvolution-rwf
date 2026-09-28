#pragma once

#include <Events/PacketEvent.h>

#include <TiltedCore/Stl.hpp>

#include <cstdint>

struct World;
struct PluginMessagingRequest;

/**
 * @brief Routes opaque companion-plugin payloads between clients.
 *
 * The server deliberately does not understand the payload. It authenticates
 * the sender, enforces the size limits, and forwards the bytes to the
 * requested target - which is exactly what a plugin needs and nothing more.
 *
 * Sender identity is taken from the connection the request arrived on, never
 * from the payload, so a plugin cannot speak for another player.
 *
 * Rate limiting lives here rather than in each plugin: a misbehaving or
 * hostile client must not be able to make the server fan out unbounded traffic
 * to every player in the session.
 */
class PluginMessagingService
{
public:
    PluginMessagingService(World& aWorld, entt::dispatcher& aDispatcher);

    // Drops the sender's token bucket. PlayerId comes from a monotonic counter
    // and is never reused, so a bucket left behind is never read again - without
    // this the map grows for the lifetime of the server, one entry per player
    // who ever connected.
    void OnPlayerRemoved(std::uint32_t aPlayerId) noexcept;

protected:
    void OnPluginMessage(const PacketEvent<PluginMessagingRequest>& acMessage) noexcept;

private:
    // Per-connection token bucket. A morph snapshot burst is legitimate; a
    // sustained flood is not, and a dropped payload degrades a plugin instead
    // of degrading the session for everybody.
    static constexpr std::uint32_t kMaxPayloadBytes = 24 * 1024;
    static constexpr std::uint32_t kMaxChannelBytes = 96;
    static constexpr std::uint32_t kBurstCapacity = 256;
    static constexpr std::uint32_t kRefillPerSecond = 128;

    [[nodiscard]] bool AllowMessage(std::uint32_t aPlayerId, std::uint32_t aBytes) noexcept;

    struct Bucket
    {
        double Tokens{ static_cast<double>(kBurstCapacity) };
        std::int64_t LastRefillMs{ 0 };
    };

    World& m_world;
    entt::scoped_connection m_pluginMessageConnection;
    TiltedPhoques::Map<std::uint32_t, Bucket> m_buckets;
};