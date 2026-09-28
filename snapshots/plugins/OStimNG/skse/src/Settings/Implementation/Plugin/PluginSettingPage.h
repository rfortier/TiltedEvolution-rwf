#pragma once

#include "../../Model/SettingPage.h"

#include "PluginInterface/Settings/SettingPage.h"

namespace Settings {
    namespace PluginSettings {
        class PluginSettingPage : public SettingPage {
        public:
            PluginSettingPage(OStim::SettingPage* page);

            virtual void onMenuOpened();
            virtual std::string getName();
            virtual SettingDisplayOrder getDisplayOrder();

            virtual settingGroupIndex getGroupCount();
            virtual SettingGroup* getGroup(settingGroupIndex index);

        private:
            OStim::SettingPage* page;
        };
    }
}