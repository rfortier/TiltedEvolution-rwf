#include <Messages/ShiftGridCellRequest.h>
#include <algorithm>

// The count field is 8 bits wide, so a longer list could not be received anyway:
// clamp it, or the reader consumes only the low 8 bits worth of entries and then
// misparses the rest of the packet.
static constexpr size_t kMaxCellCount = 0xFF;

void ShiftGridCellRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WorldSpaceId.Serialize(aWriter);
    PlayerCell.Serialize(aWriter);
    CenterCoords.Serialize(aWriter);

    const auto count = std::min<size_t>(Cells.size(), kMaxCellCount);
    aWriter.WriteBits(count, 8);

    for (size_t i = 0; i < count; ++i)
    {
        Cells[i].Serialize(aWriter);
    }
}

void ShiftGridCellRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    WorldSpaceId.Deserialize(aReader);
    PlayerCell.Deserialize(aReader);
    CenterCoords.Deserialize(aReader);

    uint64_t count = 0;
    aReader.ReadBits(count, 8);

    Cells.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        Cells[i].Deserialize(aReader);
    }
}
