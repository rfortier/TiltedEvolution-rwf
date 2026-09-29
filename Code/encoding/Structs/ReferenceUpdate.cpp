#include <Structs/ReferenceUpdate.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool ReferenceUpdate::operator==(const ReferenceUpdate& acRhs) const noexcept
{
    return UpdatedMovement == acRhs.UpdatedMovement && ActionEvents == acRhs.ActionEvents;
}

bool ReferenceUpdate::operator!=(const ReferenceUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ReferenceUpdate::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    UpdatedMovement.Serialize(aWriter);

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
    UpdatedMovement.Deserialize(aReader);

    // Never trust the wire length, and never throw: this runs inside a noexcept frame.
    const auto count = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxActionEventCount);

    ActionEvents.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        ActionEvents[i].ApplyDifferential(aReader);
    }
}
