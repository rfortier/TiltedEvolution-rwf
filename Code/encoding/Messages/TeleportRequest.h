#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct TeleportRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kTeleportRequest;

    TeleportRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const TeleportRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId; }

    // Player ids are handed out by a monotonic counter (Code/server/Game/Player.cpp) and are
    // not reused, so they are not bounded by 16 bits. The field was, and the read below masks
    // 32 bits into it: past the 65536th player the overlay teleported to somebody else.
    uint32_t PlayerId{};
};
