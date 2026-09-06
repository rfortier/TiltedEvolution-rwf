#pragma once

#include <FunctionHook.hpp>

// Every TP_HOOK in the client is routed through here so the log can say what
// the install actually found at its target. A hook that never lands is silent -
// it simply never runs - and a hook that shares its function with another mod
// comes down to who patched last; either way the consequence shows up later as
// a crash with nothing pointing back at the hook. On 1.5.x this is load
// bearing, because those target addresses are recovered by matching rather than
// published.
namespace HookAudit
{
// Remember the target a hook is about to be installed on, along with the bytes
// currently there.
void Record(void** appTargetSlot) noexcept;

// Check every recorded target against what is there now, once the hooks have
// been committed.
void Report() noexcept;

// Re-check the recorded targets later in the run: a hook can be installed
// correctly and then be overwritten by a mod that patches the same function
// afterwards, which silently stops it from ever running again.
void Verify(const char* acpWhen) noexcept;

template <class T, class U> void Add(T** appTargetSlot, U* apHookFunction) noexcept
{
    Record(reinterpret_cast<void**>(appTargetSlot));
    TiltedPhoques::FunctionHookManager::GetInstance().Add(appTargetSlot, apHookFunction, true);
}
} // namespace HookAudit
