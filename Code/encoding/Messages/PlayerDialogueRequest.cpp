#include <Messages/PlayerDialogueRequest.h>
#include <WireLimits.h>

void PlayerDialogueRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Text);
}

void PlayerDialogueRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Text = Serialization::ReadString(aReader);
    WireLimits::Clamp(Text, WireLimits::kMaxDialogueTextBytes);
}
