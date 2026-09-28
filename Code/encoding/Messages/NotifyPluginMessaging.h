#pragma once

#include "Message.h"

#include <cstdint>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

// A companion plugin payload delivered to this client, with the identity the
// server authenticated for the sender attached.
//
// SenderPlayerId is filled in by the server from the connection the request
// arrived on, never from the payload: a plugin must not be able to claim it
// speaks for somebody else.
struct NotifyPluginMessaging final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPluginMessaging;

    static constexpr std::uint32_t kMaxChannelBytes = 96;
    static constexpr std::uint32_t kMaxPayloadBytes = 24 * 1024;
    static constexpr std::uint32_t kMaxDisplayNameBytes = 64;

    NotifyPluginMessaging()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyPluginMessaging() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    String Channel;
    String SenderDisplayName;
    // The framework's PlayerId of the sender, filled in by the server from the
    // authenticated connection. A plugin echoes it back to address that peer.
    std::uint32_t SenderPlayerId{ 0 };
    bool SenderIsHost{ false };
    Vector<std::uint8_t> PluginData;
};