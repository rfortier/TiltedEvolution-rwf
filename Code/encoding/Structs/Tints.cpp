#include <Structs/Tints.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

bool Tints::Entry::operator==(const Entry& acRhs) const noexcept
{
    return Alpha == acRhs.Alpha && Type == acRhs.Type && Color == acRhs.Color && Name == acRhs.Name;
}

bool Tints::Entry::operator!=(const Entry& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

bool Tints::operator==(const Tints& acRhs) const noexcept
{
    return Entries == acRhs.Entries;
}

bool Tints::operator!=(const Tints& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Tints::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    // The count travels in 8 bits, so the entries that follow must be clamped the same
    // way, otherwise the reader and the writer disagree about the stream layout.
    const uint8_t cCount = static_cast<uint8_t>(std::min<size_t>(Entries.size(), kMaxTintCount));
    aWriter.WriteBits(cCount, 8);

    for (size_t i = 0; i < cCount; ++i)
    {
        Serialization::WriteVarInt(aWriter, Entries[i].Type);
        aWriter.WriteBits(Entries[i].Color, 32);
        Entries[i].Name.Serialize(aWriter);
        aWriter.WriteBits(*reinterpret_cast<const uint32_t*>(&Entries[i].Alpha), 32);
    }
}

void Tints::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 8);

    const auto cCount = tmp & 0xFF;

    // The message is not guaranteed to arrive into a fresh object: append semantics
    // would silently grow the list on every reuse.
    Entries.clear();

    for (auto i = 0u; i < cCount; ++i)
    {
        uint64_t buffer = 0;

        Entry entry;
        entry.Type = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

        aReader.ReadBits(buffer, 32);
        entry.Color = buffer & 0xFFFFFFFF;
        entry.Name.Deserialize(aReader);

        aReader.ReadBits(buffer, 32);
        tmp = buffer & 0xFFFFFFFF;
        entry.Alpha = *reinterpret_cast<float*>(&tmp);

        Entries.push_back(entry);
    }
}
