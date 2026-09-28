#include "Core.h"

#include "ActorProperties/ActorPropertyTable.h"
#include "Alignment/Alignments.h"
#include "Config/Config.h"
#include "Core/ThreadStarter/ThreadBuilder.h"
#include "Furniture/FurnitureTable.h"
#include "GameAPI/Game.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"
#include "PluginInterfaceImplementation/InterfaceMapImpl.h"
#include "Util/Integrity.h"
#include "Serial/Manager.h"
#include "Settings/AddonPage.h"
#include "SexToys/ToyTable.h"
#include "Sound/SoundTable.h"
#include "Trait/TraitTable.h"
#include "Util/APITable.h"
#include "Util/CompatibilityTable.h"
#include "Util/Globals.h"
#include "Util/LegacyUtil.h"
#include "Util/LookupTable.h"

namespace Core {
    void init() { 
        RNGUtil::setup();

        Interface::InterfaceMapImpl::getSingleton()->setupInterfaces();
    }

    void postLoad() {
        Config::Config::loadConfig();

        Util::Globals::setSceneIntegrityVerified(Integrity::verifySceneIntegrity());
        Util::Globals::setTranslationIntegrityVerified(Integrity::verifyTranslationIntegrity());
    }

    void postpostLoad() {
        Toys::ToyTable::getSingleton();
    }

    void dataLoaded() {
        ActorProperties::ActorPropertyTable::setup();
        Util::APITable::setupForms();
        Util::Globals::setupForms();
        Sound::SoundTable::setup();
        Graph::GraphTable::SetupActions();
        Trait::TraitTable::setup();
        Alignment::Alignments::LoadAlignments();
        Furniture::FurnitureTable::setupFurnitureTypes();
        LegacyUtil::loadLegacyScenes();
        Graph::GraphTable::setupNodes();
        Graph::GraphTable::setupSequences();

        Compatibility::CompatibilityTable::setupForms();
        Util::LookupTable::setupForms();
        Trait::TraitTable::setupForms();
        MCM::MCMTable::setupForms();
        Graph::GraphTable::setupEvents();
        Graph::GraphTable::setupOptions();

        Settings::AddonPage::getSingleton()->loadJsonAddons();
    }

    void postDataLoaded() { }


    void newSession() {
        // TODO import settings
    }

    void sessionLoaded() {
        Serialization::closeOldThreads();
        // TODO import settings
    }

    void sessionStarted() {
        Threading::ThreadBuilder::reset();
        

        if (!Util::Globals::isSceneIntegrityVerified()) {
            GameAPI::Game::showMessageBox("OStim Standalone: Scene integrity could not be verified. OStim and its addons might not work properly. Please don't report any other bugs while this issue persists.", {"Ok"}, [](unsigned int result) {});
        }

        if (!Util::Globals::isTranslationIntegrityVerified()) {
            GameAPI::Game::showMessageBox("OStim Standalone: Your english localization files appear to be modified. This will cause upwards compatibility issues. If you are a translator create a new file ending with _<YOURLANGAUGE> instead of modifying the english one.", {"Ok"}, [](unsigned int result) {});
        }
    }


    void save(GameAPI::GameSerializationInterface serial) { }

    void load(GameAPI::GameSerializationInterface serial) { }

    void revert(GameAPI::GameSerializationInterface serial) { }
}  // namespace Core