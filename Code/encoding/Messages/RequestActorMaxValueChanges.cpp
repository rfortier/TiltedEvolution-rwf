#include <Messages/RequestActorMaxValueChanges.h>
#include <algorithm>

void RequestActorMaxValueChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);

    // The reader clamps this count to ActorValues::kMaxActorValueCount, so writing
    // more entries than that would desynchronise the stream.
    const auto count = std::min<size_t>(Values.size(), ActorValues::kMaxActorValueCount);
    Serialization::WriteVarInt(aWriter, count);

    size_t written = 0;
    for (const auto& value : Values)
    {
        if (written++ >= count)
            break;

        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }
}

void RequestActorMaxValueChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
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
        auto value = Serialization::ReadFloat(aReader);
        Values.insert({key, value});
    }
}
