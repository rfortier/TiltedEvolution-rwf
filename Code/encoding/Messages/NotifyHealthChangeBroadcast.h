#pragma once

#include "Message.h"

struct NotifyHealthChangeBroadcast final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyHealthChangeBroadcast;

    // The receiver adds this to an actor's health and kills the actor when the result
    // stops being positive, so an unbounded value is a remote kill switch. A single hit
    // cannot plausibly exceed this, and a peer that claims more gets a clamped hit.
    static constexpr float kMaxHealthDelta = 100000.f;

    NotifyHealthChangeBroadcast()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyHealthChangeBroadcast& acRhs) const noexcept { return Id == acRhs.Id && DeltaHealth == acRhs.DeltaHealth && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t Id;
    float DeltaHealth;
};
