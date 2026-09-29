#include <Messages/NotifyPlayerList.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

// The count field is read back as 16 bits, so a longer map could not be received
// anyway: clamp it, or the reader consumes only the low 16 bits worth of entries
// and then misparses the rest of the packet.
static constexpr size_t kMaxPlayerCount = 0xFFFF;

void NotifyPlayerList::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Players.size(), kMaxPlayerCount);
    Serialization::WriteVarInt(aWriter, count);

    size_t written = 0;
    for (auto& player : Players)
    {
        if (written++ >= count)
            break;

        Serialization::WriteVarInt(aWriter, player.first);
        Serialization::WriteString(aWriter, player.second);
    }
}

void NotifyPlayerList::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    auto count = Serialization::ReadVarInt(aReader) & 0xFFFF;

    for (auto i = 0u; i < count; ++i)
    {
        uint32_t id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        Players[id] = Serialization::ReadString(aReader);
    }
}
