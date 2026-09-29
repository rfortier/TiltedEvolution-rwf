#include <Messages/NotifyPartyInfo.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

// The count field is 8 bits wide, so a longer list could not be received anyway:
// clamp it, or the reader consumes only the low 8 bits worth of entries and then
// misparses the rest of the packet.
static constexpr size_t kMaxPlayerCount = 0xFF;

void NotifyPartyInfo::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, IsLeader);
    Serialization::WriteVarInt(aWriter, LeaderPlayerId);

    const auto count = std::min<size_t>(PlayerIds.size(), kMaxPlayerCount);
    aWriter.WriteBits(count, 8);

    for (size_t i = 0; i < count; ++i)
    {
        Serialization::WriteVarInt(aWriter, PlayerIds[i]);
    }
}

void NotifyPartyInfo::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    IsLeader = Serialization::ReadBool(aReader);
    LeaderPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    uint64_t count = 0;
    aReader.ReadBits(count, 8);

    PlayerIds.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        PlayerIds[i] = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    }
}
