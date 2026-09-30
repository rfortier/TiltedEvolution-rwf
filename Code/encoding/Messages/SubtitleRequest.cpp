#include <Messages/SubtitleRequest.h>
#include <WireLimits.h>

void SubtitleRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteString(aWriter, Text);
    Serialization::WriteVarInt(aWriter, TopicFormId);
}

void SubtitleRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Text = Serialization::ReadString(aReader);
    WireLimits::Clamp(Text, WireLimits::kMaxDialogueTextBytes);
    TopicFormId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
