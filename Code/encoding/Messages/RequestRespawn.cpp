#include <Messages/RequestRespawn.h>
#include <WireLimits.h>

void RequestRespawn::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    Serialization::WriteVarInt(aWriter, ChangeFlags);
}

void RequestRespawn::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AppearanceBuffer = Serialization::ReadString(aReader);
    WireLimits::Clamp(AppearanceBuffer, WireLimits::kMaxAppearanceBytes);
    ChangeFlags = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
