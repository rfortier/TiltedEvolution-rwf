#pragma once

#include "../../Model/SettingGroup.h"

#include "PluginInterface/Settings/SettingGroup.h"

namespace Settings {
    namespace PluginSettings {
        class PluginSettingGroup : public SettingGroup {
        public:
            PluginSettingGroup(OStim::SettingGroup* group, bool disposable);
            ~PluginSettingGroup();

            virtual bool isDisposable();

            virtual std::string getName();
            virtual SettingDisplayOrder getDisplayOrder();

            virtual settingIndex getSettingCount();
            virtual Setting* getSetting(settingIndex index);

        private:
            OStim::SettingGroup* group;
            bool disposable;
        };
    }
}