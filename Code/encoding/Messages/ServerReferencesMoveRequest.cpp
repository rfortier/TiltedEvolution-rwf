#include <Messages/ServerReferencesMoveRequest.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

void ServerReferencesMoveRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);

    // The reader clamps this count to kMaxUpdateCount, so writing more entries than
    // that would desynchronise the stream.
    const auto count = std::min<size_t>(Updates.size(), kMaxUpdateCount);
    Serialization::WriteVarInt(aWriter, count);

    size_t written = 0;
    for (const auto& kvp : Updates)
    {
        if (written++ >= count)
            break;

        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void ServerReferencesMoveRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const auto count = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxUpdateCount);

    for (auto i = 0u; i < count; ++i)
    {
        const uint32_t cServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        Updates[cServerId].Deserialize(aReader);
    }
}
