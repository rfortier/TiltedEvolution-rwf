
#include <Structs/QuestLog.h>

#include <TiltedCore/Serialization.hpp>
#include <Structs/Mods.h>
#include <algorithm>

using TiltedPhoques::Serialization;

// The count field is 16 bits wide, so a longer log could not be received anyway:
// clamp it, or the reader consumes only the low 16 bits worth of entries and then
// misparses the rest of the packet.
static constexpr size_t kMaxEntryCount = 0xFFFF;

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
    uint64_t temp;
    aReader.ReadBits(temp, 16);

    const size_t count = temp & 0xFFFF;
    Entries.resize(count);

    for (auto& e : Entries)
    {
        e.Id.Deserialize(aReader);
        aReader.ReadBits(temp, 16);

        e.Stage = temp & 0xFFFF;
    }
}
