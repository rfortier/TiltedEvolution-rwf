#include <Structs/ActorValues.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>
#include <cmath>

using TiltedPhoques::Serialization;

bool ActorValues::operator==(const ActorValues& acRhs) const noexcept
{
    return ActorValuesList == acRhs.ActorValuesList;
}

bool ActorValues::operator!=(const ActorValues& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

bool ActorValues::IsValidActorValue(uint32_t aId, float aValue) noexcept
{
    return aId <= kMaxActorValueId && std::isfinite(aValue);
}

void ActorValues::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    // The reader clamps both counts to kMaxActorValueCount, so writing more entries
    // than that would desynchronise the stream.
    const auto count = std::min<size_t>(ActorValuesList.size(), kMaxActorValueCount);
    Serialization::WriteVarInt(aWriter, count);
    size_t written = 0;
    for (auto& value : ActorValuesList)
    {
        if (written++ >= count)
            break;

        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }

    const auto maxCount = std::min<size_t>(ActorMaxValuesList.size(), kMaxActorValueCount);
    Serialization::WriteVarInt(aWriter, maxCount);
    written = 0;
    for (auto& value : ActorMaxValuesList)
    {
        if (written++ >= maxCount)
            break;

        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }
}

void ActorValues::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // Both maps are the allocation. The catch restores the state the two clears
    // below produce before anything is inserted, so a failure reads as "no actor
    // values", which is what the sender of an empty message would have produced.
    try
    {
        // A message does not necessarily land in a fresh object, so drop whatever the
        // previous parse left behind: insert() keeps stale keys around otherwise.
        ActorValuesList.clear();
        ActorMaxValuesList.clear();

        // Never trust the wire length: a peer can claim far more entries than it sent.
        const auto count = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxActorValueCount);
        for (uint64_t i = 0; i < count; i++)
        {
            const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
            const auto value = Serialization::ReadFloat(aReader);

            // Both fields are read above so that an entry which is dropped here still
            // consumes its bytes and the rest of the stream stays aligned.
            if (!IsValidActorValue(key, value))
                continue;

            ActorValuesList.insert({key, value});
        }

        const auto maxCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxActorValueCount);
        for (uint64_t i = 0; i < maxCount; i++)
        {
            const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
            const auto value = Serialization::ReadFloat(aReader);

            // Both fields are read above so that an entry which is dropped here still
            // consumes its bytes and the rest of the stream stays aligned.
            if (!IsValidActorValue(key, value))
                continue;

            ActorMaxValuesList.insert({key, value});
        }
    }
    catch (...)
    {
        ActorValuesList.clear();
        ActorMaxValuesList.clear();
    }
}
