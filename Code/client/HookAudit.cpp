#include <TiltedOnlinePCH.h>

#include <CrashHandler.h>
#include <HookAudit.h>
#include <VersionDb.h>

#include <cstring>

namespace
{
struct RecordedHook
{
    void* pTarget{nullptr}; // the address MinHook was told to patch
    uint8_t before[8]{};    // the bytes that were there before it did
};

TiltedPhoques::Vector<RecordedHook>& Recorded() noexcept
{
    static TiltedPhoques::Vector<RecordedHook> s_recorded;
    return s_recorded;
}
} // namespace

// A resolved address can be wrong, and on legacy runtimes it can be a stub, so
// reading the bytes there must not be able to kill the process. Kept free of
// C++ objects so the __try stays legal under /EHsc.
static size_t SafeReadCode(void* apDst, const void* acpSrc, size_t aLen) noexcept
{
    size_t read = 0;
    __try
    {
        memcpy(apDst, acpSrc, aLen);
        read = aLen;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return read;
}

// The address-library id a target was resolved from, or a note that the loaded
// library does not answer for it.
//
// A collision between two hooks is only diagnosable once the ids are on the
// line. Two sites that deliberately share a target and two ids the map wrongly
// resolved to the same address produce exactly the same address, and only the
// ids tell those two cases apart - and on a legacy runtime the second case is
// the one that happens. Written into a caller buffer rather than returned as a
// string because this runs from Record(), which is noexcept.
static void FormatId(void* apTarget, char (&aBuf)[32]) noexcept
{
    unsigned long long id = 0;
    if (VersionDb::Get().FindIdByAddress(apTarget, id))
        sprintf_s(aBuf, "id %llu", id);
    else
        strcpy_s(aBuf, "no id in the loaded library");
}

// Where the branch at aFrom leads, 0 when it cannot be followed.
//
// Two forms actually turn up at an entry point another mod already owns:
//
//   e9 <rel32>     a relative jump. This is also what MinHook writes, which
//                  is how a landed hook is recognised.
//   ff 25 <rel32>  jmp qword ptr [rip+rel32]. A six byte thunk whose target
//                  address is stored in the eight bytes right after it.
//
// The second form is why a conflict used to report "no relative jump to
// follow": the destination is not an offset to add but a pointer to read,
// so the name of the module that took the function over was one dereference
// away and simply never fetched. EngineFixes leaves exactly this thunk on
// GameHeap::Allocate when its MemoryManager patch is on, and its own check
// in Games/Memory.cpp already keys off the same 0x25FF signature.
static uintptr_t BranchTarget(const uint8_t* acpCode, const uintptr_t aFrom) noexcept
{
    if (acpCode[0] == 0xE9)
    {
        int32_t displacement = 0;
        memcpy(&displacement, acpCode + 1, sizeof(displacement));

        return aFrom + 5 + displacement;
    }

    if (acpCode[0] == 0xFF && acpCode[1] == 0x25)
    {
        int32_t displacement = 0;
        memcpy(&displacement, acpCode + 2, sizeof(displacement));

        // The instruction is six bytes, so rip already points past it and the
        // stored destination sits at rip plus the displacement.
        const auto slot = aFrom + 6 + displacement;

        uintptr_t destination = 0;
        if (SafeReadCode(&destination, reinterpret_cast<const void*>(slot), sizeof(destination)) != sizeof(destination))
            return 0;

        return destination;
    }

    return 0;
}

void HookAudit::Record(void** appTargetSlot) noexcept
{
    if (!appTargetSlot || !*appTargetSlot)
    {
        // A null target means the address behind the hook never resolved, so
        // MinHook has nothing to patch and the hook is lost without a word
        spdlog::error("hook has no target to install on: the resolved address is null");
        return;
    }

    RecordedHook recorded{};
    recorded.pTarget = *appTargetSlot;

    // An id with no mapping degrades to a shared stub, and hooking it means
    // patching the stub: the hook "lands" there and never runs, and whoever
    // calls the stub gets our jump instead of a graceful zero. Say so now,
    // because later it only looks like a hook that never fires.
    if (recorded.pTarget == VersionDb::GetUnresolvedStub())
    {
        spdlog::error("hook target is the unresolved-id stub: the address library has no mapping for it, so this hook "
                      "is patched onto the fallback and can never reach the game");
    }

    for (const auto& previous : Recorded())
    {
        if (previous.pTarget == recorded.pTarget)
        {
            // Both forms, because they answer different questions. The raw
            // address is what a grep of the address library answers to; the
            // module+offset is what says *which function* the two hooks landed
            // on, and that is the whole diagnosis. Two sites that deliberately
            // share a target look identical to two ids that the map wrongly
            // resolved to the same address, and on a legacy runtime the second
            // case is the one that happens: a wrong entry in the id map shows
            // up here first, as a collision, long before it shows up as a hook
            // that never runs or as a crash.
            char where[MAX_PATH + 48];
            FormatModuleOffset(reinterpret_cast<uintptr_t>(recorded.pTarget), where);

            char idText[32];
            FormatId(recorded.pTarget, idText);

            spdlog::error("hook target {} ({:#x}, {}) is already claimed by another hook in this mod; one of the two "
                          "will be installed and one silently dropped",
                          where, reinterpret_cast<uintptr_t>(recorded.pTarget), idText);
            break;
        }
    }

    SafeReadCode(recorded.before, recorded.pTarget, sizeof(recorded.before));

    Recorded().push_back(recorded);
}

void HookAudit::Report() noexcept
{
    size_t missing = 0;
    size_t shared = 0;

    for (const auto& recorded : Recorded())
    {
        const auto target = reinterpret_cast<uintptr_t>(recorded.pTarget);

        char where[MAX_PATH + 48];
        FormatModuleOffset(target, where);

        // Print every target, not just the failing ones. A hook that is
        // installed but never reached - which is what both frame-loop hooks
        // showed on 1.5.97 - looks identical in the summary to one that works;
        // only the address list tells the two apart. The id is on the line
        // because the address alone cannot be checked against the map: it is
        // the id that says which entry to look up.
        char idText[32];
        FormatId(recorded.pTarget, idText);
        spdlog::debug("hook target {} ({})", where, idText);

        uint8_t now[8]{};
        SafeReadCode(now, recorded.pTarget, sizeof(now));

        // MinHook redirects a function by writing a 5 byte relative jump over
        // its first bytes, so finding that jump is the proof the hook landed
        if (now[0] != 0xE9)
        {
            missing++;
            spdlog::error("hook did not land on {} ({:#x}): the bytes there are {:02x} {:02x} {:02x} {:02x} {:02x}, "
                          "so this hook will never run",
                          where, target, now[0], now[1], now[2], now[3], now[4]);
            continue;
        }

        const bool wasBranch = recorded.before[0] == 0xE9 || recorded.before[0] == 0xE8 ||
                               recorded.before[0] == 0xEB ||
                               (recorded.before[0] == 0xFF && recorded.before[1] == 0x25);
        if (!wasBranch)
            continue;

        shared++;

        // Following the branch is the whole diagnosis: it names the module
        // that already owns this function, so a conflict caused by a known
        // compatibility shim reads as one instead of as an unnamed mod.
        char earlier[MAX_PATH + 48];
        strcpy_s(earlier, "not followable");
        if (const auto previous = BranchTarget(recorded.before, target))
            FormatModuleOffset(previous, earlier);

        spdlog::warn("hook target {} ({:#x}) already held a branch ({:02x} {:02x} {:02x} {:02x} {:02x}, leading to "
                     "{}): another mod hooks this function too and ours went on top of it. A target inside "
                     "EngineFixes.dll is its MemoryManager patch, which Games/Memory.cpp already replaces after "
                     "initterm; any other module is a real conflict",
                     where, target, recorded.before[0], recorded.before[1], recorded.before[2], recorded.before[3],
                     recorded.before[4], earlier);
    }

    spdlog::info("hooks: {} recorded, {} did not land, {} shared with another mod", Recorded().size(), missing, shared);
}

void HookAudit::Verify(const char* acpWhen) noexcept
{
    size_t gone = 0;

    for (const auto& recorded : Recorded())
    {
        const auto target = reinterpret_cast<uintptr_t>(recorded.pTarget);

        uint8_t now[8]{};
        SafeReadCode(now, recorded.pTarget, sizeof(now));

        if (now[0] == 0xE9)
            continue;

        gone++;

        char where[MAX_PATH + 48];
        FormatModuleOffset(target, where);

        char leadsTo[MAX_PATH + 48];
        strcpy_s(leadsTo, "not followable");
        if (const auto destination = BranchTarget(now, target))
            FormatModuleOffset(destination, leadsTo);

        spdlog::error("hook on {} ({:#x}) is gone: the bytes there are now {:02x} {:02x} {:02x} {:02x} {:02x} "
                      "(leading to {}), so ours has not run since whoever wrote them",
                      where, target, now[0], now[1], now[2], now[3], now[4], leadsTo);
    }

    spdlog::info("hooks re-checked ({}): {} of {} no longer carry our jump", acpWhen, gone, Recorded().size());
}

