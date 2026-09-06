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

#include <cstring>

#include <mem/mem.h>
#include <mem/protect.h>

#include <spdlog/spdlog.h>

#include <VersionDb.h>

namespace GamePatch
{
// A patch site inside an anchor. The offset was measured on 1.6.x and does not
// survive the recompile, so 1.5.x needs its own - and `legacy` stays at
// kUnknown until that offset is verified against the actual bytes, which no
// patch site currently is.
//
// Tools/ida/patch_offsets_1597.py aligns the two disassemblies and proposes an
// offset, but aligning "a call" to "a call" is not proof it is the same call,
// and a misplaced Nop or a SwapCall that captures a bogus callee corrupts live
// code. Every site here is quality of life (menu unfreezing, favorites
// numbering, the stats menu, thread names), so an unverified offset is not
// worth the crash risk: kUnknown skips the patch and logs it.
struct Site
{
    static constexpr size_t kUnknown = static_cast<size_t>(-1);

    size_t modern;
    size_t legacy{kUnknown};
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

// Applies the per-version offset to an anchor.
inline uint8_t* At(uint8_t* apAnchor, const Site& acSite, const char* acpWhat) noexcept
{
    if (!apAnchor)
        return nullptr;

    const size_t offset = VersionDb::Get().IsLegacyFormat() ? acSite.legacy : acSite.modern;
    if (offset == Site::kUnknown)
    {
        spdlog::warn("patch '{}' skipped: no known site on game {}", acpWhat,
                     VersionDb::Get().GetLoadedVersionString());
        return nullptr;
    }

    return apAnchor + offset;
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

    const auto target = reinterpret_cast<intptr_t>(aReplacement);
    displacement = static_cast<int32_t>(target - reinterpret_cast<intptr_t>(pSite) - 5);

    return WriteBytes(pSite + 1, &displacement, sizeof(displacement), acpWhat);
}

// Overwrites the function at apAddress with an unconditional jmp rel32.
template <class TFunc> bool Jump(void* apAddress, TFunc aReplacement, const char* acpWhat) noexcept
{
    if (!apAddress)
        return false;

    uint8_t patch[5]{0xE9};
    const auto target = reinterpret_cast<intptr_t>(aReplacement);
    const auto displacement = static_cast<int32_t>(target - reinterpret_cast<intptr_t>(apAddress) - 5);
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

    uint8_t patch[5]{0xE8};
    const auto target = reinterpret_cast<intptr_t>(aReplacement);
    const auto displacement = static_cast<int32_t>(target - reinterpret_cast<intptr_t>(apAddress) - 5);
    std::memcpy(patch + 1, &displacement, sizeof(displacement));

    return WriteBytes(apAddress, patch, sizeof(patch), acpWhat);
}
} // namespace GamePatch
