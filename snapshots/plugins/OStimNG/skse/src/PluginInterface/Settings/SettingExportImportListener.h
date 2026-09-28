#pragma once

#include "SettingExportImportScale.h"

namespace OStim {
    class SettingExportImportListener {
    public:
        /**
        * gets called when a setting export gets started, use SettingInterface.h to export settings
        */
        virtual void exportSettings(SettingExportImportScale scale) = 0;

        /**
        * gets called when a setting import gets started, use SettingInterface.h to import settings
        */
        virtual void importSettings(SettingExportImportScale scale) = 0;
    };
}