#pragma once

#include "PluginInterface/Settings/SettingInterface.h"
#include "Util/Singleton.h"

namespace Interface {
    class SettingInterfaceImpl : public OStim::SettingInterface, public Util::Singleton<SettingInterfaceImpl> {
    public:
        virtual uint32_t getVersion() override;
        virtual void addGroup(OStim::SettingGroup* group) override;
        virtual void addPage(OStim::SettingPage* page) override;
        virtual void registerExportImportListener(OStim::SettingExportImportListener* listener) override;
        virtual void exportBoolSimple(const char* addon, const char* setting, bool value) override;
        virtual void exportIntSimple(const char* addon, const char* setting, int32_t value) override;
        virtual void exportFloatSimple(const char* addon, const char* setting, float value) override;
        virtual void exportStringSimple(const char* addon, const char* setting, const char* value) override;
        virtual bool importBoolSimple(const char* addon, const char* setting, bool fallback) override;
        virtual int32_t importIntSimple(const char* addon, const char* setting, int32_t fallback) override;
        virtual float importFloatSimple(const char* addon, const char* setting, float fallback) override;
        virtual const char* importStringSimple(const char* addon, const char* setting, const char* fallback) override;
    };
}