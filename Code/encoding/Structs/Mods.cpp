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

        // A count past kMaxModCount cannot come from a well behaved peer: Serialize clamps
        // the list with std::min(ModList.size(), kMaxModCount) before it writes the count, so
        // the number on the stream is never larger than the bound. Dropping the whole list is
        // therefore the correct answer to one that is, and it costs this side no work at all.
        //
        // Reading the over-long run to keep the cursor aligned instead is worse: how many
        // entries it walks is decided by the peer's 13 bit count, not by the packet, so a three
        // byte message buys up to 8191 entries - and every one of them starts by reading a
        // length prefix that is then used to size a string. Each entry's width comes from that
        // prefix, so it cannot be derived from the bytes left (GetBytePosition/GetSize would only
        // give an upper bound on the count, not the offsets), which makes reading the run an
        // uncontrolled read rather than merely wasteful.
        const size_t modCount = static_cast<size_t>(data);
        if (modCount > kMaxModCount)
        {
            ModList.clear();
            return;
        }

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
