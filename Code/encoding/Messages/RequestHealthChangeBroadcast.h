#pragma once

#include "Message.h"

struct RequestHealthChangeBroadcast final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestHealthChangeBroadcast;

    // The receiver adds this to an actor's health and kills the actor when the result
    // stops being positive, so an unbounded value is a remote kill switch. A single hit
    // cannot plausibly exceed this, and a peer that claims more gets a clamped hit.
    static constexpr float kMaxHealthDelta = 100000.f;

    RequestHealthChangeBroadcast()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestHealthChangeBroadcast& acRhs) const noexcept { return Id == acRhs.Id && DeltaHealth == acRhs.DeltaHealth && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t Id;
    float DeltaHealth;
};
