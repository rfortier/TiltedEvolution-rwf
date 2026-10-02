#pragma once

#include "Message.h"
#include <Structs/ObjectData.h>

using TiltedPhoques::Vector;

struct AssignObjectsRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kAssignObjectsRequest;

    AssignObjectsRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~AssignObjectsRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AssignObjectsRequest& acRhs) const noexcept { return Objects == acRhs.Objects && GetOpcode() == acRhs.GetOpcode(); }

    // The count field is 8 bits wide, so a well behaved sender can never exceed this.
    // The writer clamps to it, which means anything past it would be dropped without a
    // word, so a sender that has more to say sends another packet instead.
    static constexpr size_t kMaxObjectCount = 0xFF;

    Vector<ObjectData> Objects{};
};
