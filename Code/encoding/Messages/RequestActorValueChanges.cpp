#include <Messages/RequestActorValueChanges.h>
#include <algorithm>

void RequestActorValueChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);

    // The reader clamps this count to ActorValues::kMaxActorValueCount, so writing
    // more entries than that would desynchronise the stream.
    const auto count = std::min<size_t>(Values.size(), ActorValues::kMaxActorValueCount);
    Serialization::WriteVarInt(aWriter, count);

    size_t written = 0;
    for (auto& value : Values)
    {
        if (written++ >= count)
            break;

        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }
}

void RequestActorValueChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // Each insert allocates a node, and the count above is what the wire claims
    // rather than what was sent. The map was cleared before the loop, so clearing
    // it here restores the "no values" answer an empty message would have given,
    // instead of a half applied set of actor values.
    try
    {
        ClientMessage::DeserializeRaw(aReader);

        Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

        // A message object can be reused: without this the entries of the previous
        // message would linger and be applied a second time.
        Values.clear();

        // A count past ActorValues::kMaxActorValueCount cannot come from a well behaved peer:
        // SerializeRaw clamps the map with std::min(size, ActorValues::kMaxActorValueCount)
        // before it writes the count, so the number on the stream is never larger than the
        // bound. Clamping the count here instead, which is what this used to do, walked only
        // the entries below the bound while the stream went on, so the rest of the packet was
        // read from an offset the sender never wrote. Dropping a packet that cannot exist on
        // the wire therefore costs a well behaved peer nothing.
        //
        // Consuming the over-long run so the cursor would stay aligned is not the answer
        // either: how many entries it walks is decided by the peer's count, not by the packet,
        // so a two byte header buys tens of thousands of reads. An entry is a VarInt plus a
        // float, so the bytes left (GetBytePosition/GetSize) bound the count only loosely.
        //
        // This count is the last field of the message, so returning leaves the caller with a
        // message that carries no values, which is what the sender of an empty map produces.
        const auto count = Serialization::ReadVarInt(aReader);
        if (count > ActorValues::kMaxActorValueCount)
        {
            Values.clear();
            return;
        }

        for (uint64_t i = 0; i < count; i++)
        {
            const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
            const auto value = Serialization::ReadFloat(aReader);

            // Both fields are read above so that an entry which is dropped here still
            // consumes its bytes and the rest of the stream stays aligned.
            if (!ActorValues::IsValidActorValue(key, value))
                continue;

            Values.insert({key, value});
        }
    }
    catch (...)
    {
        Values.clear();
    }
}
