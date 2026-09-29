#pragma once

#include "Message.h"

struct StringCacheUpdate final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kStringCacheUpdate;

    StringCacheUpdate()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    // The count is written as a VarInt but read back masked to 16 bits, so this is
    // the most entries one message can carry. The producer has to stop here too.
    static constexpr size_t kMaxValueCount = 0xFFFF;

    bool operator==(const StringCacheUpdate& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && StartId == achRhs.StartId && Values == achRhs.Values; }

    uint32_t StartId{0};
    TiltedPhoques::Vector<String> Values;
};
