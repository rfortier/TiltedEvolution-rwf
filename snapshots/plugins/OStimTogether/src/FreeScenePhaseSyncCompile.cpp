#include "PCH.h"

// CommonLibSSE-NG 3.5.3 does not expose Actor::StopTranslation() as a C++
// member even though the native Papyrus implementation is available. Keep the
// phase-sync source readable and compile it through the same native relocation
// already used by FreeSceneAlignmentFix.
namespace OStimTogether::PhaseSyncCompat
{
    inline void StopReferenceTranslation(RE::TESObjectREFR* object)
    {
        if (!object) {
            return;
        }

        using func_t = void(
            RE::BSScript::IVirtualMachine*,
            RE::VMStackID,
            RE::TESObjectREFR*);

        static REL::Relocation<func_t> func{
            RELOCATION_ID(55712, 56243)
        };

        func(nullptr, 0, object);
    }
}

// FreeScenePhaseSync.cpp contains one actor->StopTranslation() call. Expand
// only that call in this translation unit to the native compatibility helper.
// actor->StopTranslation();
// becomes:
// actor->GetFormID(), PhaseSyncCompat::StopReferenceTranslation(actor);
#define StopTranslation() GetFormID(), OStimTogether::PhaseSyncCompat::StopReferenceTranslation(actor)
#include "FreeScenePhaseSync.cpp"
#undef StopTranslation
