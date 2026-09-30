#include <Messages/NotifyChatMessageBroadcast.h>
#include <WireLimits.h>

void NotifyChatMessageBroadcast::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<uint64_t>(MessageType));
    Serialization::WriteString(aWriter, PlayerName);
    Serialization::WriteString(aWriter, ChatMessage);
}

void NotifyChatMessageBroadcast::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    MessageType = static_cast<ChatMessageType>(Serialization::ReadVarInt(aReader));
    PlayerName = Serialization::ReadString(aReader);
    WireLimits::Clamp(PlayerName, WireLimits::kMaxDisplayNameBytes);
    ChatMessage = Serialization::ReadString(aReader);
    WireLimits::Clamp(ChatMessage, WireLimits::kMaxChatMessageBytes);
}
