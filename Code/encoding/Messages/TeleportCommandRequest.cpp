#include <Messages/TeleportCommandRequest.h>
#include <WireLimits.h>

void TeleportCommandRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, TargetPlayer);
}

void TeleportCommandRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    TargetPlayer = Serialization::ReadString(aReader);
    WireLimits::Clamp(TargetPlayer, WireLimits::kMaxDisplayNameBytes);
}
