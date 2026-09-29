#pragma once

#include <cstdint>

#include <TiltedCore/Buffer.hpp>

using TiltedPhoques::Buffer;

struct TimeModel
{
    // Default time: 01/01/01 at 12:00
    float TimeScale = 20.f;
    float Time = 12.f;
    uint32_t Year = 1;
    uint32_t Month = 1;
    uint32_t Day = 1;

    // The engine keeps the month and the day zero based and the hour inside a single day,
    // and the server refuses a time scale above this when one is set by hand. Clamp to them
    // instead of throwing: Deserialize is called from noexcept frames, and every one of these
    // values ends up as a loop bound or as the operand of a float to integer cast.
    static constexpr float kMaxTimeScale = 1000.f;
    static constexpr float kMaxTime = 24.f;
    static constexpr uint32_t kMaxDay = 31u;
    static constexpr uint32_t kMaxMonth = 11u;
    static constexpr uint32_t kMaxYear = 999u;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const TimeModel& achRhs) const noexcept { return TimeScale == achRhs.TimeScale && Time == achRhs.Time && Day == achRhs.Day && Month == achRhs.Month && Year == achRhs.Year; }
    bool operator!=(const TimeModel& achRhs) const noexcept { return !this->operator==(achRhs); }
};
