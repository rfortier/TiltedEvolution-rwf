#include <Structs/Movement.h>
#include <WireLimits.h>
#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

bool Movement::operator==(const Movement& acRhs) const noexcept
{
    return CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation && Variables == acRhs.Variables && Direction == acRhs.Direction;
}

bool Movement::operator!=(const Movement& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Movement::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    Variables.GenerateDiff(AnimationVariables{}, aWriter);
    aWriter.WriteBits(*reinterpret_cast<const uint32_t*>(&Direction), 32);
}

void Movement::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);
    Variables = AnimationVariables{};
    Variables.ApplyDiff(aReader);

    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 32);
    uint32_t tmp32 = tmp & 0xFFFFFFFF;

    // A NaN or an infinity survives every arithmetic operation it takes part in, and this
    // value is assigned to the middle process of an actor on the receiving client without
    // being read again. The sender cannot express either one.
    Direction = WireLimits::ClampFinite(*reinterpret_cast<float*>(&tmp32));
}
