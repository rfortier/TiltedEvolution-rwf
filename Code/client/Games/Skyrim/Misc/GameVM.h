#pragma once

#include <Misc/BSScript.h>

struct SkyrimVM
{
    virtual ~SkyrimVM();

    static SkyrimVM* Get();

    // The vm as the game itself handed it to one of our papyrus hooks. That is
    // the only source that holds on both runtimes: the field below sits where
    // 1.6.x puts it, and on 1.5.97 that offset lands on unrelated data, so a
    // read there returns a non-null pointer to something that is not a vm.
    // Nothing is remembered until a hook runs, so callers still have to cope
    // with getting nothing back.
    static void SetVirtualMachine(BSScript::IVirtualMachine* apVirtualMachine) noexcept;
    static BSScript::IVirtualMachine* GetVirtualMachine() noexcept;

    // The field sits 0x10 lower on 1.5.97 than on the AE runtimes, exactly like
    // inactive below, so it gets its own value per target rather than adopting
    // upstream's. Taking upstream's 0x210 wholesale is the mistake section 2 of
    // docs/PITFALLS.md warns about for anchor-style pads, and it is what shipped
    // here: the 1.5.97 log reported the vm at "+0x200, not the +0x210 this build
    // assumes" on every session.
    //
    // What it cost was quiet. SetVirtualMachine() overwrites the static with
    // the pointer the game hands our papyrus hook, and the singleton field is
    // only the fallback for callers that get there before any hook has run, so
    // a wrong offset produced a non-null pointer that LooksLikeVirtualMachine()
    // then rejected - the papyrus path silently degraded instead of crashing.
    //
    // Evidence for 0x200 on 1.5.97, independent of that log: SkyrimVM::Update
    // (0x921F10) loads [rsi+0x200] five times, one of them behind a null check,
    // and the function called immediately after it (0x922A90) loads
    // [rcx+0x200] and calls through [rax+0x28] - a vtable call on that exact
    // pointer. The v1.0.18 fork baseline also had 0x200.
#ifdef SKYRIM_TARGET_LEGACY
    uint8_t pad8[0x200 - 0x8];
#else
    uint8_t pad8[0x210 - 0x8];
#endif
    BSScript::IVirtualMachine* virtualMachine;
    // inactive gates whether the client does any work at all: SkyrimVM64.cpp
    // reads it on every vm tick and skips World::Update() while it is non-zero.
    //
    // It is NOT at the same place on both runtimes. The pre-merge fork carried
    // a local VMContext with inactive at 0x680, which is where 1.5.97 puts it,
    // and 1.5.97 is the runtime that actually ships a 1.5.x address library
    // here. Upstream moved the field to 0x690 for the AE runtimes, and adopting
    // that value wholesale is what broke 1.5.x: the read at 0x690 lands on the
    // neighbouring field, sees a non-zero value, and stops running World::Update
    // for the rest of the session. Nothing in the log says why, and every
    // feature that needs the runner - connecting included - dies with it, since
    // the connect lambda is queued and never drained.
    //
    // The two runtimes differ by 0x10 here, the same way virtualMachine above
    // differs by 0x10, so each target asserts its own value rather than
    // adopting one offset for both.
#ifdef SKYRIM_TARGET_LEGACY
    uint8_t pad218[0x680 - 0x200 - sizeof(void*)];
    int32_t inactive;
#else
    uint8_t pad218[0x690 - 0x210 - sizeof(void*)];
    int32_t inactive;
#endif
    static constexpr int kInactiveOffset =
#ifdef SKYRIM_TARGET_LEGACY
        0x680;
#else
        0x690;
#endif
};

#ifdef SKYRIM_TARGET_LEGACY
static_assert(offsetof(SkyrimVM, virtualMachine) == 0x200);
static_assert(offsetof(SkyrimVM, inactive) == 0x680);
#else
static_assert(offsetof(SkyrimVM, virtualMachine) == 0x210);
static_assert(offsetof(SkyrimVM, inactive) == 0x690);
#endif

using GameVM = SkyrimVM;
