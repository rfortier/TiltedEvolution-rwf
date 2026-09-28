#pragma once

namespace Util {
    class LookupTable {
    public:
        inline static RE::TESQuest* OSexIntegrationMainQuest;
        inline static RE::TESPackage* OStimScenePackage;
        inline static RE::TESImageSpaceModifier* OStimNutEffect;

        static void setupForms();
    };
}