#include <Structs/CachedString.h>
#include <WireLimits.h>
#include <TiltedCore/Serialization.hpp>
#include "StringCache.h"

using TiltedPhoques::Serialization;

CachedString& CachedString::operator=(const TiltedPhoques::String& acRhs) noexcept
{
    String::operator=(acRhs);
    return *this;
}

void CachedString::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto cId = StringCache::Get()[*this];

    Serialization::WriteBool(aWriter, cId.has_value());
    if (cId)
    {
        Serialization::WriteVarInt(aWriter, *cId);
    }
    else
    {
        Serialization::WriteString(aWriter, *this);

        StringCache::Get().AddWanted(*this);
    }
}

void CachedString::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    const auto cHasId = Serialization::ReadBool(aReader);
    if (cHasId)
    {
        const auto cId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        const auto cValue = StringCache::Get()[cId];
        if (cValue)
        {
            *this = *cValue;
        }
        else
        {
            // A peer only writes an id for a string it believes we already hold, so an id
            // outside our cache means the two caches disagree - not that this code is
            // wrong. It used to assert(false), which aborts the process: this frame is
            // noexcept, so there is no way to fail softly, and the input is a packet any
            // peer can send (the default build is releasedbg, where the assert is live).
            // The name is left empty, which the consumers of a cached name already read
            // as "not one of the names I know" - ActionReplayCache.cpp:66 and :81 test
            // the name against a fixed list and fall through when it matches nothing.
            *this = TiltedPhoques::String();
        }
    }
    else
    {
        auto value = Serialization::ReadString(aReader);
        WireLimits::Clamp(value, WireLimits::kMaxCachedStringBytes);

        *this = value;

        StringCache::Get().AddWanted(*this);
    }
}
