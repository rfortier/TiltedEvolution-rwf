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
};
