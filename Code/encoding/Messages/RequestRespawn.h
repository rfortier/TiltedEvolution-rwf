#pragma once

#include "Message.h"

struct RequestRespawn final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestRespawn;

    RequestRespawn()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestRespawn& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ActorId == acRhs.ActorId && AppearanceBuffer == acRhs.AppearanceBuffer && ChangeFlags == acRhs.ChangeFlags; }

    uint32_t ActorId{};
    String AppearanceBuffer{};
    uint32_t ChangeFlags{0};
};
