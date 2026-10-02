#include <Messages/RequestPlayerHealthUpdate.h>
#include <WireLimits.h>
#include <TiltedCore/Serialization.hpp>

void RequestPlayerHealthUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Percentage);
}

void RequestPlayerHealthUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Percentage = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));
}
