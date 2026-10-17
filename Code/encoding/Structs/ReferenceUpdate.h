#pragma once

#include <Structs/Movement.h>
#include <Structs/ActionEvent.h>

using TiltedPhoques::Buffer;
using TiltedPhoques::Vector;

struct ReferenceUpdate
{
    ReferenceUpdate() = default;
    ~ReferenceUpdate() = default;

    bool operator==(const ReferenceUpdate& acRhs) const noexcept;
    bool operator!=(const ReferenceUpdate& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    // A peer that plays back its whole action history still stays in the low tens, and
    // the server replay cache itself is capped at 32. Never trust the wire length:
    // clamp instead of allocating whatever the packet claims.
    static constexpr uint64_t kMaxActionEventCount = 0x400;

    Movement UpdatedMovement{};
    Vector<ActionEvent> ActionEvents{};

    // When the owner sampled this snapshot, on the server's clock. Carried per
    // actor rather than per packet because one packet interleaves actors whose
    // snapshots were taken at different moments (the server relays each actor
    // once, whenever that actor's owner reported it). The receiver interpolates
    // against this instead of the packet's relay time, so a slow or bursty relay
    // no longer shows up as a change of walking speed.
    uint64_t SampleTick{};
};
