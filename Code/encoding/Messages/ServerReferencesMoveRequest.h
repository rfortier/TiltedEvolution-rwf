#pragma once

#include "Message.h"
#include <Structs/ReferenceUpdate.h>

using TiltedPhoques::String;

struct ServerReferencesMoveRequest final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kServerReferencesMoveRequest;

    ServerReferencesMoveRequest()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ServerReferencesMoveRequest& acRhs) const noexcept { return Updates == acRhs.Updates && Tick == acRhs.Tick && GetOpcode() == acRhs.GetOpcode(); }

    uint64_t Tick{};
    TiltedPhoques::Map<uint32_t, ReferenceUpdate> Updates{};

    // The server reports every entity the receiving player can see, so this is larger
    // than the client side limit but still bounded. Never trust the wire length: clamp
    // instead of reading whatever the packet claims.
    static constexpr uint64_t kMaxUpdateCount = 0x4000;
};
