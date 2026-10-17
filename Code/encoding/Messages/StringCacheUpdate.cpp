#include <Messages/StringCacheUpdate.h>
#include <WireLimits.h>
#include <algorithm>

void StringCacheUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, StartId);

    // The count field is 16 bits wide, so a longer list could not be received anyway:
    // clamp it, or the reader consumes only the low 16 bits worth of entries and then
    // misparses the rest of the packet.
    const auto count = std::min<size_t>(Values.size(), kMaxValueCount);
    Serialization::WriteVarInt(aWriter, count);

    for (size_t i = 0; i < count; ++i)
    {
        Serialization::WriteString(aWriter, Values[i]);
    }
}

void StringCacheUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // The value list is the allocation. The start id was read before it and stays,
    // and the receiver drops a message whose cursor does not line up with its own -
    // which an empty list at the wrong id already is.
    try
    {
        StartId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

        const auto count = Serialization::ReadVarInt(aReader) & 0xFFFF;

        // The mask above already keeps this count under the bound, so this cannot fire today;
        // it is the check every other count in the protocol gets, made before the resize.
        if (count > WireLimits::kMaxElementCount)
        {
            Values.clear();
            return;
        }

        Values.resize(count);

        for (auto i = 0ull; i < count; ++i)
        {
            auto value = Serialization::ReadString(aReader);
            WireLimits::Clamp(value, WireLimits::kMaxCachedStringBytes);

            Values[i] = std::move(value);
        }
    }
    catch (...)
    {
        Values.clear();
    }
}
