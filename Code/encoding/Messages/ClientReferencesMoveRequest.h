#pragma once

#include "Message.h"
#include <Structs/ReferenceUpdate.h>

using TiltedPhoques::String;

struct ClientReferencesMoveRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kClientReferencesMoveRequest;

    ClientReferencesMoveRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~ClientReferencesMoveRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ClientReferencesMoveRequest& acRhs) const noexcept { return Updates == acRhs.Updates && GetOpcode() == acRhs.GetOpcode(); }

    uint64_t Tick{};
    TiltedPhoques::Map<uint32_t, ReferenceUpdate> Updates{};

    // A client only ever reports the entities it owns itself, so this stays far below
    // the limit. Never trust the wire length: clamp instead of reading whatever the
    // packet claims.
    static constexpr uint64_t kMaxUpdateCount = 0x1000;
};
