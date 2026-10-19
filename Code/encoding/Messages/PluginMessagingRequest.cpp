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

    // Trailing envelope metadata, read back in this exact order.
    Serialization::WriteVarInt(aWriter, Flags);
    Serialization::WriteVarInt(aWriter, Sequence);
}

void PluginMessagingRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // Same shape as NotifyPluginMessaging above. The channel and the target were
    // read before the payload and stay; only the payload is dropped.
    try
    {
        ClientMessage::DeserializeRaw(aReader);

        Channel = Serialization::ReadString(aReader);
        if (Channel.size() > kMaxChannelBytes)
            Channel.resize(kMaxChannelBytes);

        std::uint64_t target = 0;
        aReader.ReadBits(target, 8);
        TargetKind = static_cast<Target>(target);

        TargetPlayerId = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFFu);

        // Never trust the wire length. SerializeRaw clamps what it writes to
        // kMaxPayloadBytes, so a claim past that bound can only come from a peer that is not
        // speaking this protocol - and a claim that fits the bound still says nothing about
        // how many bytes followed it. Both cases drop the payload and stop here.
        //
        // Compared as 64 bits, before any narrowing: 2^32 + 10 truncates to 10 in a
        // uint32_t and would otherwise be accepted as a legitimate small payload.
        const std::uint64_t claimedBytes = Serialization::ReadVarInt(aReader);
        if (claimedBytes > kMaxPayloadBytes)
        {
            PluginData.clear();
            return;
        }

        const auto size = static_cast<std::uint32_t>(claimedBytes);

        // Bytes actually left, spelled as a subtraction: Advance() does not clamp, so a
        // cursor already past the end would turn "position + size" into a wrapped-around
        // count that reads as free space.
        const std::size_t position = aReader.GetBytePosition();
        const std::size_t bufferSize = aReader.GetBuffer()->GetSize();
        if (position > bufferSize || static_cast<std::size_t>(size) > bufferSize - position)
        {
            PluginData.clear();
            return;
        }

        PluginData.resize(size);
        for (std::uint32_t i = 0; i < size; ++i)
        {
            std::uint64_t byte = 0;
            // ReadBits reports failure and leaves the cursor alone, so its return value is
            // the only thing separating "the wire ran out" from "the peer sent a zero
            // byte". Ignoring it stored every missing byte as 0 and the payload was then
            // dispatched as if it had arrived intact.
            if (!aReader.ReadBits(byte, 8))
            {
                PluginData.clear();
                return;
            }
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