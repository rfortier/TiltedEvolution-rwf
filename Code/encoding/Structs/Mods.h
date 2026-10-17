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

    // In the mods-mismatch reply this list is ONE list built from TWO
    // comparisons, so Entry::Id does not carry an id there - it carries which
    // comparison the entry came from. It therefore has to be a value no real id
    // can take, and 0 cannot be that value:
    //
    //   * a standard mod's index 0 is the first plugin in the load order, and
    //   * a lite mod's index 0 is the FIRST ESL - ESLoader::LoadLoadOrder hands
    //     liteId = 0 to the first one and TESFile::Setup builds its form id
    //     prefix as 0xFE000000 + liteId * 0x1000.
    //
    // Client Mod::GetId() returns exactly that index (liteId for a lite mod), so
    // a client whose first ESL is absent from the server sent Id 0 for a mod it
    // had to REMOVE, while the UI read Id 0 as "you must install this".
    static constexpr uint16_t kInstallMarker = 0xFFFF;
    static constexpr uint16_t kRemoveMarker = 0xFFFE;
    // A lite id is the middle twelve bits of a form id, so it cannot exceed this,
    // and a standard id is a uint8_t the engine fills from the load order.
    static constexpr uint16_t kMaxLiteId = 0x0FFF;
    static_assert(kRemoveMarker > kMaxLiteId,
                  "both direction markers must be values no mod id can take, or a real id reads as a direction");

    Mods() = default;
    ~Mods() = default;

    bool operator==(const Mods& acRhs) const noexcept;
    bool operator!=(const Mods& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    // The count travels on the wire in 13 bits, so a peer can name up to 8191 entries,
    // but the writer never sends more than this. The read side drops a message that names
    // more, rather than clamping it: the count drives a resize, and a clamped count leaves
    // the reader at the end of the entries it kept while the stream still holds the rest.
    static constexpr uint64_t kMaxModCount = 4096;
};
