#include <Messages/RequestFactionsChanges.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

// The count field is 8 bits wide, so a longer map could not be received anyway:
// clamp it, or the reader consumes only the low 8 bits worth of entries and then
// misparses the rest of the packet.
static constexpr size_t kMaxChangeCount = 0xFF;

void RequestFactionsChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Changes.size(), kMaxChangeCount);
    aWriter.WriteBits(count, 8);

    size_t written = 0;
    for (auto& change : Changes)
    {
        if (written++ >= count)
            break;

        Serialization::WriteVarInt(aWriter, change.first);
        change.second.Serialize(aWriter);
    }
}

void RequestFactionsChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t count = 0;
    aReader.ReadBits(count, 8);

    for (auto i = 0u; i < count; ++i)
    {
        auto& change = Changes[Serialization::ReadVarInt(aReader) & 0xFFFFFFFF];
        change.Deserialize(aReader);
    }
}
