#include <Messages/AssignObjectsResponse.h>
#include <algorithm>

// The count field is 8 bits wide, so a longer list could not be received anyway:
// clamp it, or the reader consumes only the low 8 bits worth of entries and then
// misparses the rest of the packet.
static constexpr size_t kMaxObjectCount = 0xFF;

void AssignObjectsResponse::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Objects.size(), kMaxObjectCount);
    aWriter.WriteBits(count, 8);

    for (size_t i = 0; i < count; ++i)
    {
        Objects[i].Serialize(aWriter);
    }
}

void AssignObjectsResponse::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    uint64_t count = 0;
    aReader.ReadBits(count, 8);

    Objects.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        Objects[i].Deserialize(aReader);
    }
}
