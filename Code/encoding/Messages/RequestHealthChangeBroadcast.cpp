#include <Messages/RequestHealthChangeBroadcast.h>
#include <algorithm>
#include <cmath>

void RequestHealthChangeBroadcast::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteFloat(aWriter, DeltaHealth);
}

void RequestHealthChangeBroadcast::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    // Never trust the wire: a NaN or an infinity would follow this value into every
    // arithmetic operation the receiver performs on it.
    if (!std::isfinite(DeltaHealth))
        DeltaHealth = 0.f;
    else
        DeltaHealth = std::min(std::max(DeltaHealth, -kMaxHealthDelta), kMaxHealthDelta);
}
