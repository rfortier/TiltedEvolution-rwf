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

        // A count past kMaxActorValueCount cannot come from a well behaved peer: Serialize
        // clamps each list with std::min(size, kMaxActorValueCount) before it writes the
        // count, so the number on the stream is never larger than the bound. Clamping the
        // count here instead is what used to happen, and it left the walk short of the
        // entries the sender wrote while the stream continued past them, so every field
        // behind this struct was then read from an offset the sender never wrote.
        //
        // Consuming the over-long run so the cursor would stay aligned is not the answer
        // either: how many entries it walks is decided by the peer's count, not by the
        // packet, so a two byte header buys tens of thousands of reads, and
        // docs/PITFALLS.md:330 records that what a Buffer::Reader does past its end is
        // unknown (throw, return zero, or undefined - it ships with tiltedcore, not here).
        // Dropping the message costs this side nothing at all.
        //
        // Both lists go, not just the one that overflowed: the maximum list sits behind
        // this one on the stream, so returning before it is reached is the only way to
        // stop the parse at the point where the frame was lost.
        const auto count = Serialization::ReadVarInt(aReader);
        if (count > kMaxActorValueCount)
        {
            ActorValuesList.clear();
            ActorMaxValuesList.clear();
            return;
        }

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

        // Same judgement as above, applied to the last field of this struct.
        const auto maxCount = Serialization::ReadVarInt(aReader);
        if (maxCount > kMaxActorValueCount)
        {
            ActorValuesList.clear();
            ActorMaxValuesList.clear();
            return;
        }

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
