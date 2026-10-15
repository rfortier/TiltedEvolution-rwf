#include <Messages/NotifyPluginMessaging.h>

#include <TiltedCore/Serialization.hpp>

#include <algorithm>

void NotifyPluginMessaging::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Channel);
    Serialization::WriteString(aWriter, SenderDisplayName);
    Serialization::WriteVarInt(aWriter, SenderPlayerId);
    Serialization::WriteBool(aWriter, SenderIsHost);

    const auto size = static_cast<std::uint32_t>(std::min<std::size_t>(PluginData.size(), kMaxPayloadBytes));
    Serialization::WriteVarInt(aWriter, size);
    for (std::uint32_t i = 0; i < size; ++i)
        aWriter.WriteBits(PluginData[i], 8);

    // Trailing envelope metadata, read back in this exact order.
    Serialization::WriteVarInt(aWriter, Flags);
    Serialization::WriteVarInt(aWriter, Sequence);
}

void NotifyPluginMessaging::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // The payload is the allocation, and the bound above it is what a peer may
    // claim rather than what it sent. An empty payload is a message with nothing
    // in it, which the plugins already treat as a lost one.
    try
    {
        ServerMessage::DeserializeRaw(aReader);

        Channel = Serialization::ReadString(aReader);
        if (Channel.size() > kMaxChannelBytes)
            Channel.resize(kMaxChannelBytes);

        SenderDisplayName = Serialization::ReadString(aReader);
        if (SenderDisplayName.size() > kMaxDisplayNameBytes)
            SenderDisplayName.resize(kMaxDisplayNameBytes);

        SenderPlayerId = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
        SenderIsHost = Serialization::ReadBool(aReader);

        const auto size = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader));
        const auto bounded = std::min<std::uint32_t>(size, kMaxPayloadBytes);
        PluginData.resize(bounded);
        for (std::uint32_t i = 0; i < bounded; ++i)
        {
            std::uint64_t byte = 0;
            aReader.ReadBits(byte, 8);
            PluginData[i] = static_cast<std::uint8_t>(byte & 0xFF);
        }

        Flags = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);
        Sequence = Serialization::ReadVarInt(aReader);
    }
    catch (...)
    {
        PluginData.clear();
    }
}