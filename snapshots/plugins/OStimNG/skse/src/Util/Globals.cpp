#include "Globals.h"

namespace Util {
    void Globals::setupForms() {
        auto dataHandler = RE::TESDataHandler::GetSingleton();

        OStimUsePapyrusUndressing = dataHandler->LookupForm<RE::TESGlobal>(0xDB0, "OStim.esp");
    }

    void Globals::resetSaveGameValues() {
        doPapyrusUndressing = false;
    }


    void Globals::setSceneIntegrityVerified(bool verified) {
        sceneIntegrityVerified = verified;
        logger::info("scene integrity is {}", verified ? "verified" : "not verified");
    }

    void Globals::setTranslationIntegrityVerified(bool verified) {
        translationIntegrityVerified = verified;
        logger::info("translation integrity is {}", verified ? "verified" : "not verified");
    }

    void Globals::setPapyrusUndressing(bool doPapyrus) {
        doPapyrusUndressing = doPapyrus;
        OStimUsePapyrusUndressing->value = doPapyrus ? 1 : 0;
        logger::info("papyrus undressing is {}", doPapyrus ? "enabled" : "disabled");
    }
}