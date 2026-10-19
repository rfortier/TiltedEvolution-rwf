
#include <Structs/QuestLog.h>

#include <TiltedCore/Serialization.hpp>
#include <Structs/Mods.h>
#include <algorithm>

using TiltedPhoques::Serialization;

bool QuestLog::operator==(const QuestLog& acRhs) const noexcept
{
    return Entries == acRhs.Entries;
}

bool QuestLog::operator!=(const QuestLog& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void QuestLog::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Entries.size(), kMaxEntryCount);
    aWriter.WriteBits(count, 16);

    for (size_t i = 0; i < count; ++i)
    {
        Entries[i].Id.Serialize(aWriter);

        // id's can be in the full 16 bit range
        aWriter.WriteBits(Entries[i].Stage, 16);
    }
}

void QuestLog::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // The entry list is the allocation. An empty log is the safe answer: the client
    // keeps the quest state it already has instead of a half applied one.
    try
    {
        uint64_t temp;
        aReader.ReadBits(temp, 16);

        // Never trust the wire length, and never throw: this runs inside a noexcept frame.
        // A count past kMaxEntryCount cannot come from a well behaved peer: Serialize clamps
        // the list with std::min(Entries.size(), kMaxEntryCount) before it writes the count, so
        // the number on the stream is never larger than the bound.
        //
        // This drops the message, where the read side used to clamp the count and parse that
        // many entries. Clamping was the milder of the two old mistakes and it is still wrong:
        // it stops the walk at the bound while the stream continues past it, so every field
        // behind this log - AssignCharacterRequest continues with FaceTints, the flags and
        // CurrentActorData, CharacterData with FaceTints and AllActorValues - is then read from
        // an offset the sender never wrote. A packet that has already lost the frame is not one
        // worth handing half a quest log from.
        //
        // Consuming the over-long run so the cursor stays aligned is not the answer either: how
        // many entries it walks is decided by the peer's 16 bit count, not by the packet - a two
        // byte message buys up to 65535 entries, where the drop costs zero. An entry is two
        // VarInts plus a stage, so the bytes left (GetBytePosition/GetSize) bound the count only
        // loosely.
        const auto entryCount = static_cast<size_t>(temp & 0xFFFF);
        if (entryCount > kMaxEntryCount)
        {
            Entries.clear();
            return;
        }

        // The message is not guaranteed to arrive into a fresh object: append semantics
        // would silently grow the list on every reuse.
        Entries.clear();
        Entries.resize(entryCount);

        for (auto& entry : Entries)
        {
            entry.Id.Deserialize(aReader);
            aReader.ReadBits(temp, 16);

            entry.Stage = temp & 0xFFFF;
        }
    }
    catch (...)
    {
        Entries.clear();
    }
}
