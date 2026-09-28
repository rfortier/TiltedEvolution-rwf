#pragma once

#include "SettingExportImportListener.h"
#include "SettingPage.h"

#include "../PluginInterface.h"

namespace OStim {
    class SettingInterface : public PluginInterface {
    public:
        inline static const char* NAME = "Settings";

        // --- ABI version 1 ---

        /**
        * adds a setting group to the addon page in the configuration menu
        */
        virtual void addGroup(SettingGroup* group) = 0;

        /**
        * adds a new page to the configuration menu
        * !! this is not yet implemented !!
        */
        virtual void addPage(SettingPage* page) = 0;

        /**
         * register your listener once in the post load event
         */
        virtual void registerExportImportListener(SettingExportImportListener* listener) = 0;

        /**
        * export a setting to the settings file
        */
        virtual void exportBoolSimple(const char* addon, const char* setting, bool value) = 0;
        virtual void exportIntSimple(const char* addon, const char* setting, int32_t value) = 0;
        virtual void exportFloatSimple(const char* addon, const char* setting, float value) = 0;
        virtual void exportStringSimple(const char* addon, const char* setting, const char* value) = 0;

        /**
        * import a setting from the settings file
        */
        virtual bool importBoolSimple(const char* addon, const char* setting, bool fallback) = 0;
        virtual int32_t importIntSimple(const char* addon, const char* setting, int32_t fallback) = 0;
        virtual float importFloatSimple(const char* addon, const char* setting, float fallback) = 0;
        virtual const char* importStringSimple(const char* addon, const char* setting, const char* fallback) = 0;
    };
}