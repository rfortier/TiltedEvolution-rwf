#include "PluginSettingPage.h"

#include "PluginSettingGroup.h"

namespace Settings {
    namespace PluginSettings {
        PluginSettingPage::PluginSettingPage(OStim::SettingPage* page) : page{page} {}

        void PluginSettingPage::onMenuOpened() {
            page->onMenuOpened();
        }

        std::string PluginSettingPage::getName() {
            return std::string(page->getName());
        }

        SettingDisplayOrder PluginSettingPage::getDisplayOrder() {
            return static_cast<SettingDisplayOrder>(page->getDisplayOrder());
        }


        settingGroupIndex PluginSettingPage::getGroupCount() {
            return page->getGroupCount();
        }

        SettingGroup* PluginSettingPage::getGroup(settingGroupIndex index) {
            OStim::SettingGroup* group = page->getGroup(index);
            if (!group) {
                return nullptr;
            }
            return new PluginSettingGroup(group, true);
        }
    }
}