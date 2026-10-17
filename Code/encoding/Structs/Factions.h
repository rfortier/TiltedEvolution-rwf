#pragma once

#include "Faction.h"

using TiltedPhoques::Vector;

struct Factions
{
    Factions() = default;
    ~Factions() = default;

    bool operator==(const Factions& acRhs) const noexcept;
    bool operator!=(const Factions& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    // Both counters travel on the wire in 9 bits, so a well behaved peer can never exceed
    // this. The read side drops a message that names more instead of clamping it, so the
    // entries after a count it kept are never read from an offset the clamp invented.
    static constexpr uint64_t kMaxFactionCount = 0x1FF;

    Vector<Faction> NpcFactions;
    Vector<Faction> ExtraFactions;
};
