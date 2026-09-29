#include <Structs/AnimationVariables.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>
#include <iostream>

bool AnimationVariables::operator==(const AnimationVariables& acRhs) const noexcept
{
    return Booleans == acRhs.Booleans && Integers == acRhs.Integers && Floats == acRhs.Floats;
}

bool AnimationVariables::operator!=(const AnimationVariables& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

// std::vector<bool> implementation is unspecified, but often packed reasonably.
// The spec does not guarantee contiguous memory, though, so somewhat laborious 
// translation needed. Should be better than winding down several layers to 
// TiltedPhoques::Serialization::WriteBool, though.
//
void AnimationVariables::VectorBool_to_String(const Vector<bool>& bools, TiltedPhoques::String& chars) const
{
    chars.assign((bools.size() + 7) >> 3, 0);

    auto citer = chars.begin();
    auto biter = bools.begin();
    for (uint32_t mask = 1; biter < bools.end(); mask = 1, citer++)
        for (; mask < 0x100 && biter < bools.end(); mask <<= 1)
            *citer |= *biter++ ? mask : 0;
}

// The Vector<bool> must be the correct size when called.
//
void AnimationVariables::String_to_VectorBool(const TiltedPhoques::String& chars, Vector<bool>& bools)
{
    bools.assign(bools.size(), false);

    auto citer = chars.begin();
    auto biter = bools.begin();
    // A short string simply leaves the remaining bits false instead of reading past
    // its end: the length is not guaranteed to match the caller's expectation.
    for (uint32_t mask = 1; biter < bools.end() && citer < chars.end(); mask = 1, citer++)
        for (; mask < 0x100 && biter < bools.end(); mask <<= 1)
            *biter++ = (*citer & mask) ? true : false;
}


void AnimationVariables::Load(std::istream& aInput)
{
    // Booleans are bitpacked and a bit different, not guaranteed contiguous.
    TiltedPhoques::String chars((Booleans.size() + 7) >> 3, 0);

    aInput.read(reinterpret_cast<char*>(chars.data()), chars.size());
    String_to_VectorBool(chars, Booleans);
    aInput.read(reinterpret_cast<char*>(Integers.data()), Integers.size() * sizeof(uint32_t));
    aInput.read(reinterpret_cast<char*>(Floats.data()), Floats.size() * sizeof(float));
}

void AnimationVariables::Save(std::ostream& aOutput) const
{
    // Booleans bitpacked and not guaranteed contiguous.
    TiltedPhoques::String chars;
    VectorBool_to_String(Booleans, chars);
 
    aOutput.write(reinterpret_cast<const char*>(chars.data()), chars.size());
    aOutput.write(reinterpret_cast<const char*>(Integers.data()), Integers.size() * sizeof(uint32_t));
    aOutput.write(reinterpret_cast<const char*>(Floats.data()), Floats.size() * sizeof(float));
}

// Wire format description.
//
// Sends 3 VarInts, the count of Booleans, Integers and Floats, in that order. Then sends a bitstream of the
// sum of those counts. For the Booleans, these represent the bit values for the Booleans. For the Integers and
// Floats, it represents a truth table for whether the value has changed. If values HAVE changed, they follow on 
// the stream.
//
//
void AnimationVariables::GenerateDiff(const AnimationVariables& aPrevious, TiltedPhoques::Buffer::Writer& aWriter) const
{
    // The reader clamps every count to kMaxVariableCount, so emitting more than that
    // would desynchronise the stream: clamp here as well, and write exactly as many
    // entries as the counts claim.
    const auto booleansSize = std::min<size_t>(Booleans.size(), kMaxVariableCount);
    const auto integersSize = std::min<size_t>(Integers.size(), kMaxVariableCount);
    const auto floatsSize = std::min<size_t>(Floats.size(), kMaxVariableCount);

    const size_t sizeChangedVector = booleansSize + integersSize + floatsSize;
    auto changedVector = Booleans;
    changedVector.resize(booleansSize);
    changedVector.reserve(sizeChangedVector);

    for (size_t i = 0; i < integersSize; i++)
        changedVector.push_back(aPrevious.Integers.size() != Integers.size() || aPrevious.Integers[i] != Integers[i]);
    for (size_t i = 0; i < floatsSize; i++)
        changedVector.push_back(aPrevious.Floats.size() != Floats.size() || aPrevious.Floats[i] != Floats[i]);

    // Now serialize: VarInts Booleans.size(), Integers.size(), Floats.size(),
    // then the change table bits, then changed Integers, then changed Floats.
    TiltedPhoques::Serialization::WriteVarInt(aWriter, booleansSize);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, integersSize);
    TiltedPhoques::Serialization::WriteVarInt(aWriter, floatsSize);

    TiltedPhoques::String chars;
    VectorBool_to_String(changedVector, chars);
    TiltedPhoques::Serialization::WriteString(aWriter, chars);

    auto biter = changedVector.begin() + booleansSize;
    for (size_t i = 0; i < integersSize; i++)
        if (*biter++)
            TiltedPhoques::Serialization::WriteVarInt(aWriter, Integers[i] & 0xFFFFFFFF);
    for (size_t i = 0; i < floatsSize; i++)
        if (*biter++)
            TiltedPhoques::Serialization::WriteFloat(aWriter, Floats[i]);
}

// Reads 3 VarInts that represent the size of the Booleans, Integers and Floats.
// That's followed by a bitstream in a string of the Booleans values combined
// with a Changed? truth table for Integers and Floats.
// The Changed? table is scanned and for each true bit, the corresponsing Integer
// or Float is deserialized.
// 
void AnimationVariables::ApplyDiff(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    const size_t booleansSize = std::min<uint64_t>(TiltedPhoques::Serialization::ReadVarInt(aReader), kMaxVariableCount);
    const size_t integersSize = std::min<uint64_t>(TiltedPhoques::Serialization::ReadVarInt(aReader), kMaxVariableCount);
    const size_t floatsSize   = std::min<uint64_t>(TiltedPhoques::Serialization::ReadVarInt(aReader), kMaxVariableCount);
    if (Integers.size() != integersSize)
        Integers.assign(integersSize, 0);
    if (Floats.size() != floatsSize)
        Floats.assign(floatsSize, 0.f);
    
    TiltedPhoques::Vector<bool> changedVector(booleansSize + integersSize + floatsSize);
    auto chars = TiltedPhoques::Serialization::ReadString(aReader);
    String_to_VectorBool(chars, changedVector);

    Booleans.assign(changedVector.begin(), changedVector.begin() + booleansSize);

    auto biter = changedVector.begin() + booleansSize;
    for (size_t i = 0; i < integersSize; i++)
        if (*biter++)
            Integers[i] = TiltedPhoques::Serialization::ReadVarInt(aReader);
    for (size_t i = 0; i < floatsSize; i++)
        if (*biter++)
            Floats[i] = TiltedPhoques::Serialization::ReadFloat(aReader);
}
