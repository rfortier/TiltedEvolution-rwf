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

        const auto count = std::min<uint64_t>(Serialization::ReadVarInt(aReader), ActorValues::kMaxActorValueCount);
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
