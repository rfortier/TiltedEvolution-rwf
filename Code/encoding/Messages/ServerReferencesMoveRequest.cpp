#include <Messages/ServerReferencesMoveRequest.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

void ServerReferencesMoveRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);

    // The reader clamps this count to kMaxUpdateCount, so writing more entries than
    // that would desynchronise the stream.
    const auto count = std::min<size_t>(Updates.size(), kMaxUpdateCount);
    Serialization::WriteVarInt(aWriter, count);

    size_t written = 0;
    for (const auto& kvp : Updates)
    {
        if (written++ >= count)
            break;

        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void ServerReferencesMoveRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);

    // A count past kMaxUpdateCount cannot come from a well behaved peer: SerializeRaw clamps
    // the map with std::min(size, kMaxUpdateCount) before it writes the count, so the number
    // on the stream is never larger than the bound. Clamping the count here instead, which is
    // what this used to do, walked only the entries below the bound while the stream went on,
    // and every entry behind them was then read from an offset the sender never wrote. Dropping
    // a packet that cannot exist on the wire therefore costs a well behaved peer nothing.
    //
    // Consuming the over-long run so the cursor would stay aligned is not the answer either:
    // how many entries it walks is decided by the peer's count, not by the packet, so a small
    // message buys thousands of ReferenceUpdate parses. A ReferenceUpdate carries its own
    // variable-length payload, so the bytes left (GetBytePosition/GetSize) bound the count only
    // loosely.
    //
    // This count is the last field of the message, so returning leaves Updates empty, which is
    // what the sender of an empty map produces. It is cleared first because the map is not
    // reset on entry and the operator[] below would otherwise keep entries of a previous parse.
    const auto count = Serialization::ReadVarInt(aReader);
    if (count > kMaxUpdateCount)
    {
        Updates.clear();
        return;
    }

    for (auto i = 0u; i < count; ++i)
    {
        const uint32_t cServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        Updates[cServerId].Deserialize(aReader);
    }
}
