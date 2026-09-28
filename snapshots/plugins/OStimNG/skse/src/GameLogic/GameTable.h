#pragma once

namespace GameLogic {
    class GameTable {
    public:
        static void setup();

        inline static void setCurrentCrosshairRef(RE::TESObjectREFR* ref) { currentCrosshairRef = ref; }
        inline static RE::TESObjectREFR* getCurrentCrosshairRef() { return currentCrosshairRef; }

        inline static RE::TESGlobal* getTimescale() { return Timescale; }

        inline static RE::BGSKeyword* getNPCKeyword() { return ActorTypeNPC; }
        inline static RE::BGSKeyword* getVampireKeyword() { return Vampire; }
        inline static RE::TESRace* getManakinRace() { return ManakinRace; }

        inline static RE::TESQuest* getMainQuest() { return OSexIntegrationMainQuest; }

        inline static bool improvedCamSupport() { return OStimImprovedCamSupport->value != 0; }

        inline static RE::TESFaction* getOStimEmptyFaction() { return OStimEmptyFaction; }

    private:
        inline static RE::TESObjectREFR* currentCrosshairRef = nullptr;

        inline static RE::TESGlobal* Timescale = nullptr;

        inline static RE::BGSKeyword* ActorTypeNPC = nullptr;
        inline static RE::BGSKeyword* Vampire = nullptr;
        inline static RE::TESRace* ManakinRace = nullptr;

        inline static RE::TESQuest* OSexIntegrationMainQuest = nullptr;

        inline static RE::TESGlobal* OStimImprovedCamSupport = nullptr;

        inline static RE::TESFaction* OStimEmptyFaction = nullptr;
    };
}