#pragma once

#include <cstdint>
#include <Structs/GameId.h>

using TiltedPhoques::Vector;

struct QuestLog
{
    struct Entry
    {
        GameId Id;
        uint16_t Stage;

        bool operator==(const Entry& acRhs) const noexcept { return Id == acRhs.Id && Stage == acRhs.Stage; }

        bool operator!=(const Entry& acRhs) const noexcept { return !this->operator==(acRhs); }
    };

    // The count travels in 16 bits, so a peer can name up to 65535 entries while the
    // writer never sends more than this. Clamp the read side to the same bound: the
    // count drives the resize there, and Deserialize runs inside noexcept frames, so
    // throwing is not an option either. A log stays in the hundreds at the very most,
    // so the bound costs a legitimate sender nothing.
    static constexpr size_t kMaxEntryCount = 0x1000;
    Vector<Entry> Entries{};

    QuestLog() = default;
    ~QuestLog() = default;

    bool operator==(const QuestLog& acRhs) const noexcept;
    bool operator!=(const QuestLog& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
