#include <TiltedOnlinePCH.h>

#include <CrashHandler.h>
#include <HookAudit.h>

#include <cstring>

namespace
{
struct RecordedHook
{
    void* pTarget{nullptr};
    uint8_t before[8]{};
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

// Where the 5 byte relative jump at aFrom points, 0 when there is no such jump.
static uintptr_t RelativeJumpTarget(const uint8_t* acpCode, const uintptr_t aFrom) noexcept
{
    if (acpCode[0] != 0xE9)
        return 0;

    int32_t displacement = 0;
    memcpy(&displacement, acpCode + 1, sizeof(displacement));

    return aFrom + 5 + displacement;
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

        char earlier[MAX_PATH + 48];
        strcpy_s(earlier, "no relative jump to follow");
        if (const auto previous = RelativeJumpTarget(recorded.before, target))
            FormatModuleOffset(previous, earlier);

        spdlog::warn("hook target {} ({:#x}) already held a branch ({:02x} {:02x} {:02x} {:02x} {:02x}, leading to "
                     "{}): another mod hooks this function too and ours went on top of it",
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
        strcpy_s(leadsTo, "no relative jump to follow");
        if (const auto destination = RelativeJumpTarget(now, target))
            FormatModuleOffset(destination, leadsTo);

        spdlog::error("hook on {} ({:#x}) is gone: the bytes there are now {:02x} {:02x} {:02x} {:02x} {:02x} "
                      "(leading to {}), so ours has not run since whoever wrote them",
                      where, target, now[0], now[1], now[2], now[3], now[4], leadsTo);
    }

    spdlog::info("hooks re-checked ({}): {} of {} no longer carry our jump", acpWhen, gone, Recorded().size());
}

