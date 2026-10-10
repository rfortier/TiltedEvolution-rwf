#include <Structs/Mods.h>
#include <WireLimits.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool Mods::operator==(const Mods& acRhs) const noexcept
{
    return ModList == acRhs.ModList;
}

bool Mods::operator!=(const Mods& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Mods::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    // The count travels in 13 bits, so the entries that follow must be clamped the same
    // way, otherwise the reader and the writer disagree about the stream layout.
    const uint16_t modCount = std::min<uint64_t>(ModList.size(), kMaxModCount) & 0xFFFF;
    aWriter.WriteBits(modCount, 13);

    for (size_t i = 0; i < modCount; ++i)
    {
        aWriter.WriteBits(ModList[i].Id, 16);
        Serialization::WriteBool(aWriter, ModList[i].IsLite);
        Serialization::WriteString(aWriter, ModList[i].Filename);
    }
}

void Mods::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // The mod list is the allocation and its count comes off the wire. An empty
    // list is the safe answer: the client builds no id mapping from it, and the
    // server sends the list again on the next handshake.
    try
    {
        uint64_t data = 0;
        aReader.ReadBits(data, 13);

        // The count is 13 bits wide, so a peer can name more entries than the writer ever
        // sends. Clamp to the same bound, or the resize below follows the wire.
        const size_t modCount = std::min<uint64_t>(data & 0xFFFF, kMaxModCount);
        ModList.resize(modCount);
        for (size_t i = 0; i < modCount; ++i)
        {
            aReader.ReadBits(data, 16);
            ModList[i].Id = data & 0xFFFF;
            ModList[i].IsLite = Serialization::ReadBool(aReader);
            ModList[i].Filename = Serialization::ReadString(aReader);
            WireLimits::Clamp(ModList[i].Filename, WireLimits::kMaxFileNameBytes);
        }
    }
    catch (...)
    {
        ModList.clear();
    }
}
