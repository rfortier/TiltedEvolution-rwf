#pragma once

#include <Games/Primitives.h>
#include <Havok/hkStepInfo.h>

struct bhkCharacterController : NiRefObject
{
    bool UpdateStepTiming(float aMovementDeltaTime = 0.f) noexcept;

    uint8_t pad10[0x80 - 0x10];
    hkStepInfo stepInfo;
};

// The 0x80 offset is not an upstream assumption that was copied over: it was
// read out of a real 1.5.97 SkyrimSE.exe, which is the only build whose layout
// is in question here. Searching that image's .text for the physics-timestep
// write pattern (a float load of the physics delta, the reciprocal, then stores
// to +0x88 and +0x8C through one register) yields exactly one site, at RVA
// 0xDBFDD4, inside the function at 0xDBF630. That RVA is a real symbol in
// version-1-5-97-0.bin (id 76436), and the delta global it reads
// (RVA 0x1E083A8) is id 512261 there, so both halves are named rather than
// guessed. 0x88/0x8C are deltaTime/invDeltaTime, i.e. stepInfo at 0x80, which
// is what upstream asserts for 1.6.x/1.7.x as well - the member has not moved
// between the two families, so unlike Actor there is no legacy branch here.
static_assert(offsetof(bhkCharacterController, stepInfo) == 0x80);
static_assert(offsetof(bhkCharacterController, stepInfo) + offsetof(hkStepInfo, deltaTime) == 0x88);
static_assert(offsetof(bhkCharacterController, stepInfo) + offsetof(hkStepInfo, invDeltaTime) == 0x8C);
