#include <Structs/ReferenceUpdate.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool ReferenceUpdate::operator==(const ReferenceUpdate& acRhs) const noexcept
{
    return UpdatedMovement == acRhs.UpdatedMovement && ActionEvents == acRhs.ActionEvents &&
           SampleTick == acRhs.SampleTick;
}

bool ReferenceUpdate::operator!=(const ReferenceUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ReferenceUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    UpdatedMovement.Serialize(aWriter);

    Serialization::WriteVarInt(aWriter, SampleTick);

    // The reader clamps this count to kMaxActionEventCount, so writing more entries
    // than that would desynchronise the stream.
    const auto count = std::min<size_t>(ActionEvents.size(), kMaxActionEventCount);
    Serialization::WriteVarInt(aWriter, count);

    for (size_t i = 0; i < count; ++i)
    {
        ActionEvents[i].GenerateDifferential(ActionEvent{}, aWriter);
    }
}

void ReferenceUpdate::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // The action event list is the allocation. The movement was read before it and
    // stays, so the actor is still moved; only the animations are dropped, and the
    // next update carries them again.
    try
    {
        UpdatedMovement.Deserialize(aReader);

        SampleTick = Serialization::ReadVarInt(aReader);

        // A count past kMaxActionEventCount cannot come from a well behaved peer: Serialize clamps
        // the list with std::min(size, kMaxActionEventCount) before it writes the count, so the
        // number on the stream is never larger than the bound. Clamping the count here, which is
        // what this used to do, walked only the first kMaxActionEventCount events while the
        // stream went on, so every event behind the bound was parsed from an offset the sender
        // never wrote.
        //
        // Consuming the over-long run to keep the cursor aligned is not the answer either: how
        // many events it walks is decided by the peer's count, not by the packet, so a two byte
        // header buys thousands of ActionEvent parses. An ActionEvent's width comes from its own
        // contents, so the bytes left (GetBytePosition/GetSize) bound the count only loosely.
        // Dropping the message costs this side nothing.
        //
        // The movement and the tick stay: they were read before this count and the actor is
        // still where the sender put it. Only the animations are lost, and the next update
        // carries them again.
        const auto count = Serialization::ReadVarInt(aReader);
        if (count > kMaxActionEventCount)
        {
            ActionEvents.clear();
            return;
        }

        ActionEvents.resize(count);

        for (auto i = 0u; i < count; ++i)
        {
            ActionEvents[i].ApplyDifferential(aReader);
        }
    }
    catch (...)
    {
        ActionEvents.clear();
    }
}
