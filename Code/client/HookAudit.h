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
// currently there. Returns false when that target must not be hooked at all -
// a null slot, or the shared unresolved-id stub - and has already said why.
bool Record(void** appTargetSlot) noexcept;

// Check every recorded target against what is there now, once the hooks have
// been committed.
void Report() noexcept;

// Re-check the recorded targets later in the run: a hook can be installed
// correctly and then be overwritten by a mod that patches the same function
// afterwards, which silently stops it from ever running again.
void Verify(const char* acpWhen) noexcept;

template <class T, class U> void Add(T** appTargetSlot, U* apHookFunction) noexcept
{
    // The gate matters on every runtime, not just 1.5.x. An id the loaded address
    // library does not carry resolves to the shared zero-returning stub, and MinHook
    // detours whatever bytes are at its target: installing on the stub would send
    // every other unresolved call in the client into this hook, and installing on a
    // null slot would hand address 0 to the detour engine. Neither is recoverable
    // later, and both are one missing library entry away, so they are refused here
    // rather than patched over at each call site.
    if (!Record(reinterpret_cast<void**>(appTargetSlot)))
        return;

    TiltedPhoques::FunctionHookManager::GetInstance().Add(appTargetSlot, apHookFunction, true);
}
} // namespace HookAudit
