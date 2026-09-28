#include "CompatibilityTable.h"

#include "ActorProperties/ActorPropertyTable.h"
#include "MCM/MCMTable.h"

namespace Compatibility {
    void CompatibilityTable::setupForms() {
        RE::TESDataHandler* handler = RE::TESDataHandler::GetSingleton();

        if (handler->GetLoadedModIndex("Schlongs of Skyrim.esp")) {
            SOS_SchlongifiedFaction.loadFile("Schlongs of Skyrim.esp", 0x00AFF8);
        } else {
            logger::info("SoS full is not installed.");
        }

        if (handler->GetLoadedLightModIndex("TheNewGentleman.esp")) {
            TNG_Gentified.loadFile("TheNewGentleman.esp", 0xE00);
            TNG_Ungentified.loadFile("TheNewGentleman.esp", 0xE01);
        } else {
            logger::info("TNG is not installed");
        }

        if (handler->GetLoadedModIndex("SOS - No Futanari Schlong - Addon.esp")) {
            tryAddNoSchlongFaction("SOS - No Futanari Schlong - Addon.esp", 0x000D63);
        }
        
        if (handler->GetLoadedLightModIndex("SOS - No Futanari Schlong - Addon ESL.esp")) {
            tryAddNoSchlongFaction("SOS - No Futanari Schlong - Addon ESL.esp", 0x801);
        }

        // https://www.nexusmods.com/skyrimspecialedition/mods/63882
        if (handler->GetLoadedModIndex("SOS - Pubic Hair for Females Addon.esp")) {
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x000836);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x00087D);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x0008C4);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x000954);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x000959);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x0009A1);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x0009EA);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x000A1F);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x000A20);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x000A63);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x000AA0);
            tryAddNoSchlongFaction("SOS - Pubic Hair for Females Addon.esp", 0x000D63);
        }

        // https://www.loverslab.com/files/file/10318-sos-female-pubic-hairs-colored-for-npcs/
        if (handler->GetLoadedLightModIndex("SOS - Female Pubics.esp")) {
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x000802);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x000849);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x000890);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x0008D7);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x00091E);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x000965);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x0009AC);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x0009F3);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x000A3A);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x000A83);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x000B02);
            tryAddNoSchlongFaction("SOS - Female Pubics.esp", 0x000D63);
        }
    }

    bool CompatibilityTable::hasSchlong(GameAPI::GameActor actor) {
        GameAPI::GameSex propertySex = ActorProperties::ActorPropertyTable::getActorSex(actor);
        if (propertySex != GameAPI::GameSex::AGENDER) {
            return propertySex == GameAPI::GameSex::MALE;
        }

        if (MCM::MCMTable::useSoSSex()) {
            if (!SOS_SchlongifiedFaction.contains(actor)) {
                return false;
            }

            for (GameAPI::GameFaction faction : noSchlongFactions) {
                if (faction.contains(actor)) {
                    return false;
                }
            }

            return true;
        } else if (MCM::MCMTable::useTNGSex()) {
            if (actor.isSex(GameAPI::GameSex::MALE)) {
                return !TNG_Ungentified.contains(actor);
            } else {
                return TNG_Gentified.contains(actor);
            }
        }

        return actor.isSex(GameAPI::GameSex::MALE);
    }

    void CompatibilityTable::tryAddNoSchlongFaction(std::string modName, RE::FormID formID) {
        GameAPI::GameFaction faction;
        faction.loadFile(modName, formID);
        if (faction) {
            noSchlongFactions.push_back(faction);
        }
    }
}