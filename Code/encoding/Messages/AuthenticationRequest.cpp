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
    // This is the first message a connection can send, so it arrives before anything has
    // been authenticated: every string below is an allocation a stranger chose the size of.
    // The frame is noexcept and the fields are read one after another, so a failure part way
    // through leaves a request that is half the peer's and half default - and the half that
    // was already read is the one the server acts on. Failing here clears what the reader
    // filled, which leaves Version empty; the handshake compares it against BUILD_COMMIT
    // before it looks at the token or the mods, so an unreadable request ends as a version
    // mismatch and a kick rather than as a request with no mods and a valid password.
    try
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
    catch (...)
    {
        Token.clear();
        Version.clear();
        Username.clear();
        UserMods.ModList.clear();
    }
}
