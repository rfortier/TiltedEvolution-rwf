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
        // A count past kMaxFactionCount cannot come from a well behaved peer: Serialize
        // clamps each list with std::min(size, kMaxFactionCount) before it writes the count,
        // so the number on the stream is never larger than the bound. Dropping the message is
        // therefore the correct answer to one that is, and it costs this side no work at all.
        //
        // Consuming the over-long run to keep the cursor aligned instead - reading the entries
        // past the bound so the fields behind this struct start where the sender put them - is
        // worse: how many entries it walks is decided by the peer's count, not by the packet, so
        // a three byte message buys tens of thousands of Deserialize calls. The work is bounded
        // only by the peer's own count: unlike es_loader, which can ask Buffer::Reader how many
        // bytes are really left (GetBytePosition/GetSize), this loop consumes entries whose size
        // it does not know in advance, so "read the run and discard it" is an uncontrolled read
        // bought with a 3 byte message. That is a worse failure than the discard it replaces.
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

        // The second list sits behind the first on the stream, so a count refused here is just
        // as undefined a read as one refused above, and it has already been misparsed by the
        // time it arrives: both lists go.
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
