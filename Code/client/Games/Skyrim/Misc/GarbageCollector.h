#pragma once

struct TESBoundObject;

struct GarbageCollector
{
    // Null when the running game has no mapping for the singleton id (1.5.x):
    // callers must then leave the object alone rather than dereference the
    // zero-returning stub the address library hands out for unmapped ids.
    static GarbageCollector* Get() noexcept;

    // Uses the engine's immediate/deferred base-object deletion policy.
    void Add(TESBoundObject* apObject) noexcept;
};
