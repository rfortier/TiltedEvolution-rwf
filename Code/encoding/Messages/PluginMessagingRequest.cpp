#include <Messages/PluginMessagingRequest.h>

#include <TiltedCore/Serialization.hpp>

#include <algorithm>

void PluginMessagingRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Channel);
    aWriter.WriteBits(static_cast<std::uint8_t>(TargetKind), 8);
    Serialization::WriteVarInt(aWriter, TargetPlayerId);

    // Clamped, not asserted: a plugin that hands us an oversized payload gets a
    // truncated message rather than a corrupt stream that desynchronises every
    // later message on the connection.
    const auto size = static_cast<std::uint32_t>(std::min<std::size_t>(PluginData.size(), kMaxPayloadBytes));
    Serialization::WriteVarInt(aWriter, size);

    // Written one byte at a time, matching how every other variable-length
    // field in this codebase is serialised.
    for (std::uint32_t i = 0; i < size; ++i)
        aWriter.WriteBits(PluginData[i], 8);
}

void PluginMessagingRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Channel = Serialization::ReadString(aReader);
    if (Channel.size() > kMaxChannelBytes)
        Channel.resize(kMaxChannelBytes);

    std::uint64_t target = 0;
    aReader.ReadBits(target, 8);
    TargetKind = static_cast<Target>(target);

    TargetPlayerId = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);

    // Never trust the wire length: a peer can claim more bytes than it sent.
    const auto size = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader));
    const auto bounded = std::min<std::uint32_t>(size, kMaxPayloadBytes);
    PluginData.resize(bounded);
    for (std::uint32_t i = 0; i < bounded; ++i)
    {
        std::uint64_t byte = 0;
        aReader.ReadBits(byte, 8);
        PluginData[i] = static_cast<std::uint8_t>(byte & 0xFF);
    }
}