#pragma once

#include <cstdint>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

struct Mods
{
    struct Entry
    {
        String Filename;
        uint16_t Id;
        bool IsLite;

        bool operator==(const Entry& acRhs) const noexcept { return Filename == acRhs.Filename && Id == acRhs.Id && IsLite == acRhs.IsLite; }

        bool operator!=(const Entry& acRhs) const noexcept { return !this->operator==(acRhs); }
    };

    Vector<Entry> ModList{};

    Mods() = default;
    ~Mods() = default;

    bool operator==(const Mods& acRhs) const noexcept;
    bool operator!=(const Mods& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    // The count travels on the wire in 13 bits, so a peer can name up to 8191 entries,
    // but the writer never sends more than this. Clamp the read side to the same bound:
    // the count drives a resize, and Deserialize is called from noexcept frames.
    static constexpr uint64_t kMaxModCount = 4096;
};
