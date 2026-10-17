#include <Messages/NotifyFactionsChanges.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

static const uint64_t kMaxChangeCount = 0x400;

void NotifyFactionsChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Changes.size(), kMaxChangeCount);
    Serialization::WriteVarInt(aWriter, count);

    size_t written = 0;
    for (auto& change : Changes)
    {
        if (written++ >= count)
            break;

        Serialization::WriteVarInt(aWriter, change.first);
        change.second.Serialize(aWriter);
    }
}

void NotifyFactionsChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    uint64_t count = Serialization::ReadVarInt(aReader);

    // Early abort as we don't want to allocate a ton of memory.
    //
    // Strictly greater: the writer sends min(Changes.size(), kMaxChangeCount), so a
    // batch of exactly kMaxChangeCount entries is a full, legitimate packet. Rejecting
    // it at >= dropped every saturated update on the floor.
    if (count > kMaxChangeCount)
        return;

    for (auto i = 0u; i < count; ++i)
    {
        auto& change = Changes[Serialization::ReadVarInt(aReader) & 0xFFFFFFFF];
        change.Deserialize(aReader);
    }
}
