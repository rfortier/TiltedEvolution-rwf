#pragma once

#include "Message.h"

#include <cstdint>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

// One opaque payload for a companion plugin, carried over the framework's own
// session instead of a private socket or a chat envelope.
//
// The framework never interprets PluginData: it moves bytes and attaches the
// authenticated sender identity, which is the whole contract the plugin layer
// needs. Channel names are namespaced by the plugin and are not validated here
// beyond length, so a plugin cannot smuggle a length that overflows the
// reader - the payload size is bounded on both ends.
struct PluginMessagingRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPluginMessagingRequest;

    // Generous enough for a morph snapshot, small enough that a malicious or
    // broken client cannot make the server allocate without bound.
    static constexpr std::uint32_t kMaxChannelBytes = 96;
    static constexpr std::uint32_t kMaxPayloadBytes = 24 * 1024;

    // Delivery targets, mirroring the plugin-facing API.
    enum class Target : std::uint8_t
    {
        kServer = 1,
        kHost = 2,
        kPlayer = 3,
        kAllPlayers = 4
    };

    PluginMessagingRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~PluginMessagingRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    String Channel;
    Target TargetKind{ Target::kAllPlayers };
    // Only meaningful for Target::kPlayer. This is the framework's PlayerId, the
    // identity a client can actually learn about its peers, not the server's
    // transport connection handle - which a client is never told and could not
    // address with.
    std::uint32_t TargetPlayerId{ 0 };
    Vector<std::uint8_t> PluginData;
};