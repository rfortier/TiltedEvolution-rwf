#pragma once

struct TESNPC;
struct Actor;

// Provides helpers for synchronizing leveled NPC identities.
struct LeveledNpcSystem
{
    // Returns whether a non-temporary NPC base uses a leveled-character template.
    static bool IsLeveledNpcBase(const TESNPC* apBase) noexcept;

    // The placed NPC owns the template flags and all non-inherited data.
    // A resolved actor retains it in ExtraLeveledCreature::originalBase.
    static TESNPC* GetOriginalBase(const Actor* apActor) noexcept;

    // Whether this build can record the chosen pick in the actor's extra data.
    // The engine call behind SetLeveledCreature has no address-library mapping
    // on 1.5.x, where it degrades to a no-op, so the extra data keeps whatever
    // the engine resolved locally and a pick comparison against it never
    // succeeds. Callers that use that comparison to detect completion have to
    // know, or they retry the rebuild forever.
    static bool CanRecordPick() noexcept;

    // Rebuilds the actor's base from its original NPC and the selected template,
    // updates leveled-creature metadata, and disposes of the old temporary base.
    // Requires an actor that has finished disabling and has no 3D.
    // Returns false if the base cannot be rebuilt, leaving the actor unchanged.
    //
    // Callers must gate this on CanRecordPick(): the rebuild is only worth doing
    // where the chosen pick can also be recorded, because that record is what
    // says the rebuild finished. Where it is not, the caller keeps the previous
    // path instead (base := pick).
    static bool ApplyPick(Actor* apActor, TESNPC* apPick) noexcept;
};
