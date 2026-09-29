#pragma once

#include <cstdint>

using TiltedPhoques::Vector;

struct AnimationVariables
{
    Vector<bool> Booleans{};
    Vector<uint32_t> Integers{};
    Vector<float> Floats{};

    bool operator==(const AnimationVariables& acRhs) const noexcept;
    bool operator!=(const AnimationVariables& acRhs) const noexcept;

    void Load(std::istream&);
    void Save(std::ostream&) const;

    void GenerateDiff(const AnimationVariables& aPrevious, TiltedPhoques::Buffer::Writer& aWriter) const;
    void ApplyDiff(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    void VectorBool_to_String(const Vector<bool>& bools, TiltedPhoques::String& chars) const;
    void String_to_VectorBool(const TiltedPhoques::String& chars, Vector<bool>& bools);

    // The sizes come in as raw VarInts. The real ones are dictated by the animation
    // graph descriptors of the game and stay in the low hundreds, so a well behaved
    // peer never gets close to this. Clamp instead of allocating whatever the packet
    // claims: ApplyDiff is reached from noexcept frames, so throwing is not an option.
    static constexpr uint64_t kMaxVariableCount = 0x1000;
};
