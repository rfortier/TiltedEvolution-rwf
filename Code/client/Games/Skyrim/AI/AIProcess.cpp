#include "AIProcess.h"

#include <Havok/bhkCharacterController.h>

bhkCharacterController* AIProcess::GetCharController() noexcept
{
    // Unmapped on 1.5.97, where VersionDb hands out the shared stub: calling it
    // returns zero, which is the contract VersionDb.h documents for function
    // ids and is why the null return below is the expected answer there rather
    // than an error. (A *data* id cannot lean on that - see
    // bhkCharacterController.cpp, which resolves its id explicitly because the
    // stub would be dereferenced instead of called.)
    TP_THIS_FUNCTION(TGetCharController, bhkCharacterController*, AIProcess);
    POINTER_SKYRIMSE(TGetCharController, getCharController, 39856);
    return TiltedPhoques::ThisCall(getCharController, this);
}

void AIProcess::KnockExplosion(Actor* apActor, const NiPoint3* aSourceLocation, float afMagnitude)
{
    TP_THIS_FUNCTION(TKnockExplosion, void, AIProcess, Actor*, const NiPoint3*, float);
    POINTER_SKYRIMSE(TKnockExplosion, knockExplosion, 39895);
    TiltedPhoques::ThisCall(knockExplosion, this, apActor, aSourceLocation, afMagnitude);
}
