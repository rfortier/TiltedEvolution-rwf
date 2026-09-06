#include <TiltedOnlinePCH.h>

#include <Misc/GameVM.h>

#include <CrashHandler.h>

#include <atomic>

// Where this build's struct puts the vm pointer inside the singleton. It is a
// 1.6.x offset, so on 1.5.97 it reads something else entirely.
static constexpr size_t kAssumedVirtualMachineOffset = 0x200;

namespace
{
std::atomic<BSScript::IVirtualMachine*> s_pVirtualMachine{nullptr};
}

// Reading through a game pointer that may not be what we think must not be able
// to kill the process; kept free of C++ objects so the __try stays legal.
static size_t SafeRead(void* apDst, const void* acpSrc, size_t aLen) noexcept
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

// A vm object lives on the heap and its first field is a vtable inside a loaded
// image. Anything that fails both tests is not a vm, however non-null it looks,
// and handing it out is a crash with the wrong story attached to it.
static bool LooksLikeVirtualMachine(const void* acpCandidate) noexcept
{
    if (!acpCandidate)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(acpCandidate, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;

    if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE)
        return false;

    void* pVTable = nullptr;
    if (SafeRead(&pVTable, acpCandidate, sizeof(pVTable)) != sizeof(pVTable) || !pVTable)
        return false;

    return VirtualQuery(pVTable, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.State == MEM_COMMIT &&
           mbi.Type == MEM_IMAGE;
}

// Where inside the singleton the vm pointer really sits on this runtime, so a
// struct offset that belongs to another game version becomes a logged number
// instead of a mystery. Nothing is fixed up from this: it is what tells the next
// person which offset to correct.
static void ReportVirtualMachineOffset(const BSScript::IVirtualMachine* acpVirtualMachine) noexcept
{
    auto* pInstance = SkyrimVM::Get();
    if (!pInstance)
    {
        spdlog::warn("the game handed us the papyrus vm at {:#x} while the SkyrimVM singleton is still null, so its "
                     "layout cannot be checked",
                     reinterpret_cast<uintptr_t>(acpVirtualMachine));
        return;
    }

    void* fields[0x400 / sizeof(void*)]{};
    const size_t got = SafeRead(fields, pInstance, sizeof(fields));

    for (size_t i = 0; i < got / sizeof(void*); i++)
    {
        if (fields[i] != acpVirtualMachine)
            continue;

        const size_t offset = i * sizeof(void*);
        if (offset == kAssumedVirtualMachineOffset)
            return;

        spdlog::warn("the SkyrimVM singleton at {:#x} holds the papyrus vm at +{:#x}, not the +{:#x} this build "
                     "assumes; every other offset into this struct is suspect for the same reason",
                     reinterpret_cast<uintptr_t>(pInstance), offset, kAssumedVirtualMachineOffset);
        return;
    }

    char assumed[MAX_PATH + 48];
    FormatModuleOffset(reinterpret_cast<uintptr_t>(fields[kAssumedVirtualMachineOffset / sizeof(void*)]), assumed);

    spdlog::warn("the papyrus vm {:#x} is nowhere in the first {:#x} bytes of the SkyrimVM singleton at {:#x}, which "
                 "holds {} at the assumed +{:#x}; the singleton id or its layout is wrong on this runtime",
                 reinterpret_cast<uintptr_t>(acpVirtualMachine), sizeof(fields),
                 reinterpret_cast<uintptr_t>(pInstance), assumed, kAssumedVirtualMachineOffset);
}

SkyrimVM* SkyrimVM::Get()
{
    POINTER_SKYRIMSE(SkyrimVM*, s_instance, 400475);

    return *s_instance.Get();
}

void SkyrimVM::SetVirtualMachine(BSScript::IVirtualMachine* apVirtualMachine) noexcept
{
    if (!apVirtualMachine || s_pVirtualMachine.load() == apVirtualMachine)
        return;

    s_pVirtualMachine.store(apVirtualMachine);

    static std::atomic<bool> s_checked{false};
    if (!s_checked.exchange(true))
        ReportVirtualMachineOffset(apVirtualMachine);
}

BSScript::IVirtualMachine* SkyrimVM::GetVirtualMachine() noexcept
{
    if (auto* pVirtualMachine = s_pVirtualMachine.load())
        return pVirtualMachine;

    // No hook has seen the vm yet, so the singleton field is all there is. It is
    // only worth returning when it survives the sanity check above, because a
    // wrong offset yields a non-null pointer that no null check can catch.
    auto* pInstance = Get();
    if (!pInstance)
        return nullptr;

    void* pFromSingleton = nullptr;
    if (SafeRead(&pFromSingleton, reinterpret_cast<const uint8_t*>(pInstance) + kAssumedVirtualMachineOffset,
                 sizeof(pFromSingleton)) != sizeof(pFromSingleton))
        return nullptr;

    if (!LooksLikeVirtualMachine(pFromSingleton))
        return nullptr;

    return static_cast<BSScript::IVirtualMachine*>(pFromSingleton);
}
