#include <TiltedOnlinePCH.h>

#include <Games/TES.h>

INISettingCollection* INISettingCollection::Get() noexcept
{
    POINTER_SKYRIMSE(INISettingCollection*, settingCollection, 411155);

    // The id resolves to a *pointer to* the singleton, so two separate things can
    // be null and both are fatal to the caller:
    //   - the id itself, which on a legacy runtime with no mapping would hand back
    //     the shared executable stub, and dereferencing that reads the stub's own
    //     opcodes as a pointer (the same hazard GarbageCollector::Get() and
    //     bhkCharacterController::PhysicsDeltaTime() resolve explicitly for);
    //   - the singleton behind it, which the engine only fills once the ini
    //     settings exist, so it is null during the very early frames.
    // Callers dereference the result immediately, so returning null is the only
    // safe answer and they each check for it.
    auto* ppCollection = settingCollection.Get();
    return ppCollection ? *ppCollection : nullptr;
}

Setting* INISettingCollection::GetSetting(const char* acpName) noexcept
{
    if (!acpName)
        return nullptr;

    // Mirrors the engine's own INISettingCollection::GetSetting (SkyrimSE.exe
    // +0x3104d0): it starts at the embedded head, treats an entry whose `setting`
    // is null as "no entry here", and returns null rather than reading through a
    // null `setting`. The previous version compared `pCurrent->setting->name`
    // unconditionally, so an empty collection - head.setting == null - faulted
    // instead of answering "not found", which is what the engine returns.
    //
    // The offset the engine uses for that walk is the same one this struct
    // declares: `lea 0x118(%rcx),%rax` for the head, `0x8(%rax)` for `next`,
    // `(%rax)` for `setting`, and `0x10(%rdi)` for `setting->name`.
    for (Entry* pCurrent = &head; pCurrent; pCurrent = pCurrent->next)
    {
        if (pCurrent->setting && _stricmp(acpName, pCurrent->setting->name) == 0)
            return pCurrent->setting;
    }

    return nullptr;
}
