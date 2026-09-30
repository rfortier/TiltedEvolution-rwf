#include <Messages/AuthenticationRequest.h>
#include <WireLimits.h>

void AuthenticationRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, DiscordId);
    Serialization::WriteBool(aWriter, SKSEActive);
    Serialization::WriteBool(aWriter, MO2Active);
    Serialization::WriteString(aWriter, Token);
    Serialization::WriteString(aWriter, Version);
    UserMods.Serialize(aWriter);
    Serialization::WriteString(aWriter, Username);
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Level);
    PlayerTime.Serialize(aWriter);
}

void AuthenticationRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    DiscordId = Serialization::ReadVarInt(aReader);
    SKSEActive = Serialization::ReadBool(aReader);
    MO2Active = Serialization::ReadBool(aReader);
    Token = Serialization::ReadString(aReader);
    WireLimits::Clamp(Token, WireLimits::kMaxTokenBytes);
    Version = Serialization::ReadString(aReader);
    WireLimits::Clamp(Version, WireLimits::kMaxVersionBytes);
    UserMods.Deserialize(aReader);
    Username = Serialization::ReadString(aReader);
    WireLimits::Clamp(Username, WireLimits::kMaxDisplayNameBytes);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Level = Serialization::ReadVarInt(aReader) & 0xFFFF;
    PlayerTime.Deserialize(aReader);
}
