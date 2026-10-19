#pragma once

struct ActorValues
{
    ActorValues() = default;
    ~ActorValues() = default;

    bool operator==(const ActorValues& acRhs) const noexcept;
    bool operator!=(const ActorValues& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    // The game only has kActorValueCount actor values, so a well behaved peer can never
    // send more: Serialize clamps the list to this bound before it writes the count, so a
    // larger count on the wire did not come from a peer that obeys the same writer. Deserialize
    // drops the whole list for one instead of clamping, because clamping would stop the walk at
    // the bound while the stream kept going and put every field behind it on an offset the
    // sender never wrote. It cannot throw either way: Deserialize runs in a noexcept frame.
    static constexpr uint64_t kMaxActorValueCount = 164;

    // The keys are indices into that same table and the engine is handed them as is,
    // so a peer that names an index past the end reaches code which never checks it.
    static constexpr uint32_t kMaxActorValueId = static_cast<uint32_t>(kMaxActorValueCount - 1);

    static bool IsValidActorValue(uint32_t aId, float aValue) noexcept;

    TiltedPhoques::Map<uint32_t, float> ActorValuesList{};
    TiltedPhoques::Map<uint32_t, float> ActorMaxValuesList{};
};
