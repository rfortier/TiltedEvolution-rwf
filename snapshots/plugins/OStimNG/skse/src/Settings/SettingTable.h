#pragma once

#include "ExportImport/ExportInstance.h"
#include "ExportImport/ImportInstance.h"
#include "Model/SettingPage.h"

#include "PluginInterface/Settings/SettingExportImportListener.h"
#include "Util/Singleton.h"

namespace Settings {
    class SettingTable : public Util::Singleton<SettingTable> {
    public:
        SettingTable();

        inline void addExportImportListener(OStim::SettingExportImportListener* listener) { exportImportListeners.push_back(listener); }

        void menuOpenend();
        inline void addPage(SettingPage* page) { pages.push_back(page); }
        inline size_t getPageCount() { return pages.size(); }
        SettingPage* getPage(int index);

        void writeJson(json& json);
        void readJson(json& json);

        inline ExportImport::ExportInstance* getExportInstance() { return exportInstance; }
        inline ExportImport::ImportInstance* getImportInstance() { return importInstance; }

    private:
        std::vector<SettingPage*> pages;
        std::vector<OStim::SettingExportImportListener*> exportImportListeners;

        ExportImport::ExportInstance* exportInstance = nullptr;
        ExportImport::ImportInstance* importInstance = nullptr;
    };
}