#pragma once

#include "Message.h"

struct NotifySetTimeResult final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifySetTimeResult;

    NotifySetTimeResult()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifySetTimeResult& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && Result == achRhs.Result; }

    enum class SetTimeResult : uint8_t
    {
        kSuccess,
        kNoPermission,
        // The request was understood and authorized, but the hour or the minute in it
        // was not a time. Appended, so the two values above keep their bytes.
        kInvalidTime
    };

    SetTimeResult Result{};
};
