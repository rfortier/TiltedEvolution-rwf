#pragma once

// Byte level patches applied to the game image.
//
// Two very different environments load this client and they disagree on one
// detail that matters here: the launcher maps SkyrimSE.exe itself and marks
// every section PAGE_EXECUTE_READWRITE (immersive_launcher/loader/
// ExeLoader.cpp), while under SKSE the Windows loader maps .text
// PAGE_EXECUTE_READ. TiltedPhoques::Put/Nop/SwapCall write straight through
// without touching page protection - fine in the launcher, an access
// violation under SKSE. The helpers below open a temporary RWX window
// instead, so the same patch works in both.
//
// They also refuse to patch through an id the loaded address library cannot
// resolve. VersionDbPtr substitutes a shared 7-byte stub for unmapped ids so
// *calls* degrade into a no-op instead of crashing, but a patch site aimed at
// that stub is a different story: writing at a small offset lands inside the
// stub's page and quietly corrupts the one function every unresolved id in the
// client calls, a jmp written over it sends all of them into whichever hook
// owned the patch, and a large offset walks off the single committed page. On
// the 1.5.x map nine of the ids used as patch anchors are unmapped, so this is
// the normal case there, not an edge case.

#include <Windows.h>

#include <cstdint>
#include <cstring>

#include <mem/mem.h>
#include <mem/protect.h>

#include <spdlog/spdlog.h>

#include <VersionDb.h>

// Declared in the client PCH and defined by whichever host loaded this runtime
// (the launcher serves it from a buffer beside the manually mapped exe, the
// SKSE build scans for free pages within +-1GB of the game module). Used here
// to stage branches that a rel32 cannot reach.
extern void* RipAllocateN(size_t blockLength);

namespace GamePatch
{
// A patch site inside an anchor. The offset was measured on 1.6.x and does not
// survive the recompile, so 1.5.x needs its own.
//
// Tools/ida/patch_offsets_1597.py aligns the two disassemblies instruction by
// instruction and reports where each site moved, refusing any candidate whose
// mnemonic or length disagrees; the results are checked in as
// Tools/ida/st_patch_offsets_1597.tsv. That is evidence, not proof - aligning
// "a call" to "a call" is not what makes it the same call - so it is used only
// where the alignment held, and every site carrying a 1.5.x offset is quality
// of life (menu unfreezing, favorites numbering, the stats menu, the DInput
// cooperative level). Where no offset has that backing, kUnknown skips the
// patch and logs it rather than aim at the 1.6.x one.
struct Site
{
    static constexpr size_t kUnknown = static_cast<size_t>(-1);

    size_t modern;
    size_t legacy{kUnknown};

    // Which 1.5.x build `legacy` was measured on, as a version-string prefix.
    // Ten different 1.5.x address libraries ship with the client and their
    // code is not interchangeable, so an offset measured on 1.5.97 must not be
    // applied to 1.5.3 just because both count as legacy.
    const char* legacyMeasuredOn{nullptr};
};

// Resolves an address library id for use as a patch anchor. Unlike
// VersionDbPtr this never substitutes the unresolved stub: a patch whose
// anchor is unknown has to be skipped, not redirected somewhere writable.
inline uint8_t* Anchor(const uint32_t acId, const char* acpWhat) noexcept
{
    auto* pAddress = static_cast<uint8_t*>(VersionDb::Get().FindAddressById(acId));
    if (!pAddress)
    {
        spdlog::warn("patch '{}' skipped: address library id {} is not mapped on game {}", acpWhat, acId,
                     VersionDb::Get().GetLoadedVersionString());
    }

    return pAddress;
}

// True when the running build is the 1.5.x version acpVersion, i.e. the one a
// checked-in 1.5.x measurement was taken against. Ten different 1.5.x address
// libraries ship and their code is not interchangeable, so an offset measured
// on 1.5.97 is not evidence for 1.5.3.
inline bool IsMeasuredFor(const char* acpVersion) noexcept
{
    return VersionDb::Get().IsLegacyFormat() &&
           VersionDb::Get().GetLoadedVersionString().rfind(acpVersion, 0) == 0;
}

// Applies the per-version offset to an anchor.
inline uint8_t* At(uint8_t* apAnchor, const Site& acSite, const char* acpWhat) noexcept
{
    if (!apAnchor)
        return nullptr;

    if (!VersionDb::Get().IsLegacyFormat())
        return apAnchor + acSite.modern;

    if (acSite.legacy == Site::kUnknown || !acSite.legacyMeasuredOn || !IsMeasuredFor(acSite.legacyMeasuredOn))
    {
        spdlog::warn("patch '{}' skipped: no site inside this function is verified for game {}", acpWhat,
                     VersionDb::Get().GetLoadedVersionString());
        return nullptr;
    }

    return apAnchor + acSite.legacy;
}

inline bool WriteBytes(void* apAddress, const void* acpData, const size_t acSize, const char* acpWhat) noexcept
{
    if (!apAddress)
        return false;

    {
        const mem::protect scope({apAddress, acSize});
        if (!scope)
        {
            spdlog::error("patch '{}' failed: cannot unprotect {} bytes at {}", acpWhat, acSize, fmt::ptr(apAddress));
            return false;
        }

        std::memcpy(apAddress, acpData, acSize);
    }

    FlushInstructionCache(GetCurrentProcess(), apAddress, acSize);
    return true;
}

template <class T> bool Put(void* apAddress, const T acValue, const char* acpWhat) noexcept
{
    return WriteBytes(apAddress, &acValue, sizeof(T), acpWhat);
}

inline bool Nop(void* apAddress, const size_t acLength, const char* acpWhat) noexcept
{
    if (!apAddress)
        return false;

    {
        const mem::protect scope({apAddress, acLength});
        if (!scope)
        {
            spdlog::error("patch '{}' failed: cannot unprotect {} bytes at {}", acpWhat, acLength, fmt::ptr(apAddress));
            return false;
        }

        std::memset(apAddress, 0x90, acLength);
    }

    FlushInstructionCache(GetCurrentProcess(), apAddress, acLength);
    return true;
}

// The runtime is not always within reach of a rel32 branch, and this is the
// difference between the two launch paths that bites hardest:
//
//   - the launcher maps the runtime beside the game image, so a direct
//     call/jmp to one of our hooks is a short hop;
//   - under SKSE the Windows loader places it wherever ASLR decides, about
//     20 GB away from the game image in practice.
//
// A 5 byte rel32 written at 20 GB carries a displacement the cast to int32_t
// silently truncates, so the branch lands in unmapped memory and the game dies
// with "jumped to code that is not there" pointing at a normal game function.
// Stage such a transfer through a stub allocated next to the game image: the
// branch stays short, the stub carries the full 64-bit target, and because a
// jmp does not touch the stack the callee still returns to the game.
inline uint8_t* NearStub(const void* acpTarget) noexcept
{
    // RipAllocateN already hands out executable pages within +-1GB of the game
    // module, which is what the rel32 needs.
    auto* pStub = static_cast<uint8_t*>(RipAllocateN(16));
    if (!pStub)
        return nullptr;

    // jmp qword ptr [rip+0]   followed by the absolute target
    const int32_t kZero = 0;
    pStub[0] = 0xFF;
    pStub[1] = 0x25;
    std::memcpy(pStub + 2, &kZero, sizeof(kZero));
    std::memcpy(pStub + 6, &acpTarget, sizeof(acpTarget));
    FlushInstructionCache(GetCurrentProcess(), pStub, 14);

    return pStub;
}

// True when the rel32 written at apFrom reaches apTo without truncation.
inline bool Reaches(const void* apFrom, const void* apTo) noexcept
{
    const int64_t delta = reinterpret_cast<int64_t>(apTo) - reinterpret_cast<int64_t>(apFrom) - 5;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

// The address a rel32 at apFrom should actually hold, going through a near
// stub when the real target is out of range. Null means give up and log.
inline const void* BranchTarget(void* apFrom, const void* apTo, const char* acpWhat) noexcept
{
    if (Reaches(apFrom, apTo))
        return apTo;

    if (auto* pStub = NearStub(apTo))
        return pStub;

    spdlog::error("patch '{}' skipped: target {} is too far from {} for a rel32 and no near stub could be allocated",
                  acpWhat, fmt::ptr(apTo), fmt::ptr(apFrom));
    return nullptr;
}

// Redirects a single `call rel32` site: reads the current target into
// aOriginal and points the instruction at aReplacement. Refuses anything
// that is not a direct call, which is what turns a wrong anchor (a mapped id
// whose intra-function offset only holds on another game version) into a
// logged no-op instead of a corrupted function.
template <class TFunc> bool SwapCall(void* apAddress, TFunc& aOriginal, TFunc aReplacement, const char* acpWhat) noexcept
{
    if (!apAddress)
        return false;

    auto* pSite = static_cast<uint8_t*>(apAddress);
    if (*pSite != 0xE8)
    {
        spdlog::error("patch '{}' skipped: no call at {} (found {:#04x}), the offset does not hold on game {}", acpWhat,
                      fmt::ptr(pSite), *pSite, VersionDb::Get().GetLoadedVersionString());
        return false;
    }

    int32_t displacement = 0;
    std::memcpy(&displacement, pSite + 1, sizeof(displacement));
    aOriginal = reinterpret_cast<TFunc>(pSite + 5 + displacement);

    const void* pTarget = BranchTarget(pSite, reinterpret_cast<const void*>(aReplacement), acpWhat);
    if (!pTarget)
        return false;

    displacement = static_cast<int32_t>(reinterpret_cast<intptr_t>(pTarget) - reinterpret_cast<intptr_t>(pSite) - 5);

    return WriteBytes(pSite + 1, &displacement, sizeof(displacement), acpWhat);
}

// Overwrites the function at apAddress with an unconditional jmp rel32.
template <class TFunc> bool Jump(void* apAddress, TFunc aReplacement, const char* acpWhat) noexcept
{
    if (!apAddress)
        return false;

    const void* pTarget = BranchTarget(apAddress, reinterpret_cast<const void*>(aReplacement), acpWhat);
    if (!pTarget)
        return false;

    uint8_t patch[5]{0xE9};
    const auto displacement = static_cast<int32_t>(reinterpret_cast<intptr_t>(pTarget) - reinterpret_cast<intptr_t>(apAddress) - 5);
    std::memcpy(patch + 1, &displacement, sizeof(displacement));

    return WriteBytes(apAddress, patch, sizeof(patch), acpWhat);
}

// Writes a fresh `call rel32` where the original instruction was already
// replaced (typically by Nop). Unlike SwapCall it does not recover the old
// target, so there is nothing to validate beyond the anchor.
template <class TFunc> bool PutCall(void* apAddress, TFunc aReplacement, const char* acpWhat) noexcept
{
    if (!apAddress)
        return false;

    const void* pTarget = BranchTarget(apAddress, reinterpret_cast<const void*>(aReplacement), acpWhat);
    if (!pTarget)
        return false;

    uint8_t patch[5]{0xE8};
    const auto displacement = static_cast<int32_t>(reinterpret_cast<intptr_t>(pTarget) - reinterpret_cast<intptr_t>(apAddress) - 5);
    std::memcpy(patch + 1, &displacement, sizeof(displacement));

    return WriteBytes(apAddress, patch, sizeof(patch), acpWhat);
}
} // namespace GamePatch
