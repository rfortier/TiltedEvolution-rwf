#include <Messages/RequestObjectInventoryChanges.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

// The count field is read back as 8 bits, so a longer map could not be received
// anyway: clamp it, or the reader consumes only the low 8 bits worth of entries
// and then misparses the rest of the packet.
static constexpr size_t kMaxChangeCount = 0xFF;

void RequestObjectInventoryChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Changes.size(), kMaxChangeCount);
    Serialization::WriteVarInt(aWriter, count);

    size_t written = 0;
    for (auto& change : Changes)
    {
        if (written++ >= count)
            break;

        change.first.Serialize(aWriter);
        change.second.Serialize(aWriter);
    }
}

void RequestObjectInventoryChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    auto count = Serialization::ReadVarInt(aReader) & 0xFF;

    for (auto i = 0u; i < count; ++i)
    {
        GameId gameId;
        gameId.Deserialize(aReader);

        auto& change = Changes[gameId];
        change.Deserialize(aReader);
    }
}
