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
    // Both lists are the allocation and both were sized from the wire. Empty lists
    // are the safe answer: the consumer sees an actor with no faction changes.
    try
    {
        // Never trust the wire length, and never throw: this runs inside a noexcept frame.
        // Clamping the count would leave the reader at the end of the entries it kept while
        // the stream still holds the rest, so a count past the bound drops the message
        // instead. Both lists go, because the second one sits behind the first on the stream.
        const auto npcCount = Serialization::ReadVarInt(aReader);
        if (npcCount > kMaxFactionCount)
        {
            NpcFactions.clear();
            ExtraFactions.clear();
            return;
        }

        NpcFactions.resize(npcCount);
        for (auto& entry : NpcFactions)
        {
            entry.Deserialize(aReader);
        }

        const auto extraCount = Serialization::ReadVarInt(aReader);
        if (extraCount > kMaxFactionCount)
        {
            NpcFactions.clear();
            ExtraFactions.clear();
            return;
        }

        ExtraFactions.resize(extraCount);
        for (auto& entry : ExtraFactions)
        {
            entry.Deserialize(aReader);
        }
    }
    catch (...)
    {
        NpcFactions.clear();
        ExtraFactions.clear();
    }
}
