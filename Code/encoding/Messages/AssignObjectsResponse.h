#pragma once

#include "Message.h"

#include <Structs/ObjectData.h>

using TiltedPhoques::Vector;

struct AssignObjectsResponse final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kAssignObjectsResponse;

    AssignObjectsResponse()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AssignObjectsResponse& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && Objects == achRhs.Objects; }

    // The count field is 8 bits wide, so a well behaved sender can never exceed this.
    // The writer clamps to it, which means anything past it would be dropped without a
    // word, so a sender that has more to say sends another packet instead.
    static constexpr size_t kMaxObjectCount = 0xFF;

    Vector<ObjectData> Objects{};
};
