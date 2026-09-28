#include <TiltedOnlinePCH.h>

#include <Misc/GarbageCollector.h>

GarbageCollector* GarbageCollector::Get() noexcept
{
    // Deliberately not POINTER_SKYRIMSE: 1.5.x has no mapping for this id, and
    // VersionDbPtr answers an unmapped id with the shared zero-returning stub,
    // so the upstream `*s_singleton.Get()` would dereference address 0 there.
    // Null lets the caller keep the old temporary base alive instead.
    auto* ppSingleton = static_cast<GarbageCollector**>(VersionDb::Get().FindAddressById(400329));
    if (!ppSingleton)
        return nullptr;

    return *ppSingleton;
}

void GarbageCollector::Add(TESBoundObject* apObject) noexcept
{
    // The base-object overload used by Actor::RecalcLeveledActor (among other
    // functions). Unmapped on 1.5.x too, where the stub makes this a no-op: the
    // temporary base is leaked rather than freed, which is the trade the rest
    // of the legacy path already makes.
    TP_THIS_FUNCTION(TAdd, void, GarbageCollector, TESBoundObject*);
    POINTER_SKYRIMSE(TAdd, s_add, 36460);
    TiltedPhoques::ThisCall(s_add, this, apObject);
}
