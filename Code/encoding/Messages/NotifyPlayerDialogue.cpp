#include <Messages/NotifyPlayerDialogue.h>
#include <WireLimits.h>

void NotifyPlayerDialogue::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Name);
    Serialization::WriteString(aWriter, Text);
}

void NotifyPlayerDialogue::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Name = Serialization::ReadString(aReader);
    WireLimits::Clamp(Name, WireLimits::kMaxDisplayNameBytes);
    Text = Serialization::ReadString(aReader);
    WireLimits::Clamp(Text, WireLimits::kMaxDialogueTextBytes);
}
