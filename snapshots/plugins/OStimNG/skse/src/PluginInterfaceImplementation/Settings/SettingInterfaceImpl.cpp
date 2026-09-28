#include "SettingInterfaceImpl.h"

#include "Settings/Implementation/Plugin/PluginSettingGroup.h"
#include "Settings/Implementation/Plugin/PluginSettingPage.h"
#include "Settings/AddonPage.h"
#include "Settings/SettingTable.h"

namespace Interface {
    uint32_t SettingInterfaceImpl::getVersion() {
        return SKSE::PluginDeclaration::GetSingleton()->GetVersion().pack();
    }

    void SettingInterfaceImpl::addGroup(OStim::SettingGroup* group) {
        if (group->isDisposable()) {
            return;
        }

        Settings::AddonPage::getSingleton()->addSkseGroup(new Settings::PluginSettings::PluginSettingGroup(group, false));
    }

    void SettingInterfaceImpl::addPage(OStim::SettingPage* page) {
        // TODO add page to stuff
    }


    void SettingInterfaceImpl::registerExportImportListener(OStim::SettingExportImportListener* listener) {
        Settings::SettingTable::getSingleton()->addExportImportListener(listener);
    }


    void SettingInterfaceImpl::exportBoolSimple(const char* addon, const char* setting, bool value) {
        Settings::ExportImport::ExportInstance* instance = Settings::SettingTable::getSingleton()->getExportInstance();
        if (!instance) {
            return;
        }

        instance->writeBoolSimple(addon, setting, value);
    }

    void SettingInterfaceImpl::exportIntSimple(const char* addon, const char* setting, int32_t value) {
        Settings::ExportImport::ExportInstance* instance = Settings::SettingTable::getSingleton()->getExportInstance();
        if (!instance) {
            return;
        }

        instance->writeIntSimple(addon, setting, value);
    }

    void SettingInterfaceImpl::exportFloatSimple(const char* addon, const char* setting, float value) {
        Settings::ExportImport::ExportInstance* instance = Settings::SettingTable::getSingleton()->getExportInstance();
        if (!instance) {
            return;
        }

        instance->writeFloatSimple(addon, setting, value);
    }

    void SettingInterfaceImpl::exportStringSimple(const char* addon, const char* setting, const char* value) {
        Settings::ExportImport::ExportInstance* instance = Settings::SettingTable::getSingleton()->getExportInstance();
        if (!instance) {
            return;
        }

        instance->writeStringSimple(addon, setting, value);
    }


    bool SettingInterfaceImpl::importBoolSimple(const char* addon, const char* setting, bool fallback) {
        Settings::ExportImport::ImportInstance* instance = Settings::SettingTable::getSingleton()->getImportInstance();
        if (!instance) {
            return fallback;
        }

        return instance->readBoolSimple(addon, setting, fallback);
    }

    int32_t SettingInterfaceImpl::importIntSimple(const char* addon, const char* setting, int32_t fallback) {
        Settings::ExportImport::ImportInstance* instance = Settings::SettingTable::getSingleton()->getImportInstance();
        if (!instance) {
            return fallback;
        }

        return instance->readIntSimple(addon, setting, fallback);
    }

    float SettingInterfaceImpl::importFloatSimple(const char* addon, const char* setting, float fallback) {
        Settings::ExportImport::ImportInstance* instance = Settings::SettingTable::getSingleton()->getImportInstance();
        if (!instance) {
            return fallback;
        }

        return instance->readFloatSimple(addon, setting, fallback);
    }

    const char* SettingInterfaceImpl::importStringSimple(const char* addon, const char* setting, const char* fallback) {
        Settings::ExportImport::ImportInstance* instance = Settings::SettingTable::getSingleton()->getImportInstance();
        if (!instance) {
            return fallback;
        }

        return instance->readStringSimple(addon, setting, fallback)->c_str();
    }
}