#include <Messages/SyncExperienceRequest.h>
#include <WireLimits.h>

void SyncExperienceRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Experience);
}

void SyncExperienceRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Experience = WireLimits::ClampExperienceDelta(Serialization::ReadFloat(aReader));
}
