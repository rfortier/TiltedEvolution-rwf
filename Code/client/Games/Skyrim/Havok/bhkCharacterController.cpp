#include <Havok/bhkCharacterController.h>

namespace
{
    // 389089 is a *data* id - the float global holding the physics timestep -
    // and it has no mapping in the 1.5.97 address library.
    //
    // Reading it through POINTER_SKYRIMSE would be wrong there even though the
    // result happens to be harmless: an unmapped id on a legacy runtime resolves
    // to the shared zero-returning stub, which is executable memory, so
    // dereferencing it as a float reads the stub's own opcodes. Those bytes
    // (48 31 C0 0F 57 C0 C3) decode to 1.895e-29, which UpdateDeltaTime rejects
    // on the <= 0.0001 rule - so the current stub is safe by luck of its
    // encoding, not by design. A data id has no business going through the stub
    // at all, and relying on a timestep validator to catch a misread of code
    // bytes is not a contract worth keeping.
    //
    // Resolving explicitly and reporting failure is what GarbageCollector::Get()
    // and LeveledNpcSystem::CanRecordPick() already do for their unmapped data
    // ids. Asked once: the mapping cannot change while the process lives.
    float* PhysicsDeltaTime() noexcept
    {
        static float* s_pDelta = static_cast<float*>(VersionDb::Get().FindAddressById(389089));
        return s_pDelta;
    }
}

bool bhkCharacterController::UpdateStepTiming(float aMovementDeltaTime) noexcept
{
    // Same physics timestep used by the native rigid-body controller movement
    // path. Where it is unavailable 0 is passed, and UpdateDeltaTime falls
    // through to the movement delta and then to the value already in the
    // controller - upstream's degradation ladder, minus the read of an address
    // that does not denote a timestep on this runtime.
    const float* pPhysicsDeltaTime = PhysicsDeltaTime();
    return stepInfo.UpdateDeltaTime(pPhysicsDeltaTime ? *pPhysicsDeltaTime : 0.f, aMovementDeltaTime);
}
