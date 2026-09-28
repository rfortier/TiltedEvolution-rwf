#include <TiltedOnlinePCH.h>

#include <Systems/LeveledNpcSystem.h>
#include <Actor.h>
#include <Components/TESActorBaseData.h>
#include <ExtraData/ExtraLeveledCreature.h>
#include <Forms/TESNPC.h>
#include <Misc/GarbageCollector.h>

bool LeveledNpcSystem::IsLeveledNpcBase(const TESNPC* apBase) noexcept
{
    if (!apBase || apBase->IsTemporary())
        return false;

    const TESForm* pTemplate = apBase->actorData.baseTemplateForm;
    return pTemplate && pTemplate->formType == FormType::LeveledCharacter;
}

TESNPC* LeveledNpcSystem::GetOriginalBase(const Actor* apActor) noexcept
{
    if (!apActor)
        return nullptr;

    const auto* pExtra = static_cast<ExtraLeveledCreature*>(apActor->extraData.GetByType(ExtraDataType::LeveledCreature));
    if (pExtra && pExtra->originalBase)
        return Cast<TESNPC>(pExtra->originalBase);

    auto* pBase = Cast<TESNPC>(apActor->baseForm);
    return IsLeveledNpcBase(pBase) ? pBase : nullptr;
}

bool LeveledNpcSystem::CanRecordPick() noexcept
{
    // Queried directly rather than through POINTER_SKYRIMSE: the point is to
    // learn whether the id resolves, and VersionDbPtr hides exactly that by
    // substituting a stub for an unmapped one.
    //
    // Asked once, not per call: this sits on the leveled-NPC reconciliation
    // path, which runs per pending actor per update, and the answer cannot
    // change while the process lives.
    static const bool s_canRecordPick = VersionDb::Get().FindAddressById(20231) != nullptr;
    return s_canRecordPick;
}

bool LeveledNpcSystem::ApplyPick(Actor* apActor, TESNPC* apPick) noexcept
{
    if (!apPick)
        return false;

    // Skyrim resolves a leveled NPC by copying the original base, then
    // applying the pick according to that base's template flags. Using
    // the pick itself discards data such as a hold guard's name/outfit.
    auto* pOriginalBase = GetOriginalBase(apActor);
    auto* pResolvedBase = pOriginalBase ? TESActorBaseData::CreateTemplateActorBase(pOriginalBase, apPick) : nullptr;
    if (!pResolvedBase)
        return false;

    auto* pOldBase = Cast<TESNPC>(apActor->baseForm);

    // Skipped where unmapped rather than sent into the no-op stub: the write is
    // what makes the rebuild observable, so a caller that got here without it
    // has no way to tell the rebuild apart from one that never ran.
    if (CanRecordPick())
        apActor->SetLeveledCreature(pOriginalBase, apPick);

    apActor->SetObjectReference(pResolvedBase);

    // Match RecalcLeveledActor's disposal policy, but never dispose of
    // a static pick left by the old reconciliation implementation. The
    // collector is null where the game version has no mapping for it, and the
    // old base then stays allocated - a leak, but one that beats handing a
    // valid pointer to the unresolved-id stub.
    if (auto* pCollector = GarbageCollector::Get())
    {
        if (pOldBase && pOldBase->IsTemporary() && pOldBase != pOriginalBase && pOldBase != apPick)
            pCollector->Add(pOldBase);
    }

    spdlog::info("Applied leveled NPC pick for actor {:X}, original base: {:X}, base: {:X}, pick: {:X}",
        apActor->formID, pOriginalBase->formID, pResolvedBase->formID, apPick->formID);
    return true;
}
