#include "SettingTable.h"

#include "AddonPage.h"

#include "SexToys/Menu/ToySettingMenu.h"
#include "SexToys/Settings/Settings.h"

namespace Settings {
    SettingTable::SettingTable() {
        addPage(Toys::Menu::ToySettingMenu::getSingleton());
        addPage(AddonPage::getSingleton());
    }


    void SettingTable::menuOpenend() {
        for (SettingPage* page : pages) {
            page->onMenuOpened();
        }
    }

    SettingPage* SettingTable::getPage(int index) {
        if (index < 0 || index >= pages.size()) {
            return nullptr;
        }

        return pages[index];
    }


    void SettingTable::writeJson(json& json) {
        Toys::Settings::Settings::getSingleton()->writeJson(json);

        exportInstance = new ExportImport::ExportInstance(json);
        for (OStim::SettingExportImportListener* listener : exportImportListeners) {
            listener->exportSettings(OStim::SettingExportImportScale::GAME);
        }
        delete exportInstance;
        exportInstance = nullptr;
    }

    void SettingTable::readJson(json& json) {
        Toys::Settings::Settings::getSingleton()->loadJson(json);

        importInstance = new ExportImport::ImportInstance(json);
        for (OStim::SettingExportImportListener* listener : exportImportListeners) {
            listener->importSettings(OStim::SettingExportImportScale::GAME);
        }
        delete importInstance;
        importInstance = nullptr;
    }
}