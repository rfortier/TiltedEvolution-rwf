#include <Structs/Factions.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool Factions::operator==(const Factions& acRhs) const noexcept
{
    return NpcFactions == acRhs.NpcFactions && ExtraFactions == acRhs.ExtraFactions;
}

bool Factions::operator!=(const Factions& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Factions::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    // Limit the number of factions to send
    const auto npcCount = std::min<uint64_t>(NpcFactions.size(), kMaxFactionCount);
    Serialization::WriteVarInt(aWriter, npcCount);

    for (uint64_t i = 0; i < npcCount; ++i)
    {
        NpcFactions[i].Serialize(aWriter);
    }

    const auto extraCount = std::min<uint64_t>(ExtraFactions.size(), kMaxFactionCount);
    Serialization::WriteVarInt(aWriter, extraCount);

    for (uint64_t i = 0; i < extraCount; ++i)
    {
        ExtraFactions[i].Serialize(aWriter);
    }
}

void Factions::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // Never trust the wire length, and never throw: this runs inside a noexcept frame.
    const auto npcCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxFactionCount);

    NpcFactions.resize(npcCount);
    for (auto& entry : NpcFactions)
    {
        entry.Deserialize(aReader);
    }

    const auto extraCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxFactionCount);

    ExtraFactions.resize(extraCount);
    for (auto& entry : ExtraFactions)
    {
        entry.Deserialize(aReader);
    }
}
