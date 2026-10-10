
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
        const auto count = std::min<size_t>(temp & 0xFFFF, kMaxEntryCount);

        // The message is not guaranteed to arrive into a fresh object: append semantics
        // would silently grow the list on every reuse.
        Entries.clear();

        for (size_t i = 0; i < count; ++i)
        {
            Entry entry;
            entry.Id.Deserialize(aReader);
            aReader.ReadBits(temp, 16);

            entry.Stage = temp & 0xFFFF;
            Entries.push_back(entry);
        }
    }
    catch (...)
    {
        Entries.clear();
    }
}
