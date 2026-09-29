#include <Structs/TimeModel.h>
#include <TiltedCore/Serialization.hpp>

#include <algorithm>
#include <cmath>

using TiltedPhoques::Serialization;

namespace
{
// A value that is not a finite number cannot be clamped into a range, and both a NaN and an
// infinity would poison every arithmetic operation the caller performs on it afterwards.
float ClampFloat(float aValue, float aMax) noexcept
{
    if (!std::isfinite(aValue))
        return 0.f;

    return std::min(std::max(0.f, aValue), aMax);
}

uint32_t ClampUint(uint32_t aValue, uint32_t aMax) noexcept
{
    return std::min(aValue, aMax);
}
}

void TimeModel::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, TimeScale);
    Serialization::WriteFloat(aWriter, Time);
    Serialization::WriteVarInt(aWriter, Day);
    Serialization::WriteVarInt(aWriter, Month);
    Serialization::WriteVarInt(aWriter, Year);
}

void TimeModel::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    TimeScale = Serialization::ReadFloat(aReader);
    Time = Serialization::ReadFloat(aReader);
    Day = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Month = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Year = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    // Never trust the wire. The month decides how many times the server walks its month lengths
    // and the day how many times it subtracts one of them, while the time and the time scale are
    // added to a running clock and then divided down to an integer.
    TimeScale = ClampFloat(TimeScale, kMaxTimeScale);
    Time = ClampFloat(Time, kMaxTime);
    Day = ClampUint(Day, kMaxDay);
    Month = ClampUint(Month, kMaxMonth);
    Year = ClampUint(Year, kMaxYear);
}
