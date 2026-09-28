#include "LookupTable.h"

namespace Util {
    void LookupTable::setupForms() {
        auto handler = RE::TESDataHandler::GetSingleton();

        OSexIntegrationMainQuest = handler->LookupForm<RE::TESQuest>(0x801, "OStim.esp");
        OStimScenePackage = handler->LookupForm<RE::TESPackage>(0xDE1, "OStim.esp");
        OStimNutEffect = handler->LookupForm<RE::TESImageSpaceModifier>(0x805, "OStim.esp");
    }
}