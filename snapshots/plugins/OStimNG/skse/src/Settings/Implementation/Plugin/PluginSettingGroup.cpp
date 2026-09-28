#include "PluginSettingGroup.h"

#include "PluginSetting.h"

namespace Settings {
    namespace PluginSettings {
        PluginSettingGroup::PluginSettingGroup(OStim::SettingGroup* group, bool disposable)
            : group{group}, disposable{disposable} {}

        PluginSettingGroup::~PluginSettingGroup() {
            if (group->isDisposable()) {
                delete group;
            }
        }


        bool PluginSettingGroup::isDisposable() {
            return disposable;
        }


        std::string PluginSettingGroup::getName() {
            return std::string(group->getName());
        }

        SettingDisplayOrder PluginSettingGroup::getDisplayOrder() {
            return static_cast<SettingDisplayOrder>(group->getDisplayOrder());
        }


        settingIndex PluginSettingGroup::getSettingCount() {
            return group->getSettingCount();
        }

        Setting* PluginSettingGroup::getSetting(settingIndex index) {
            OStim::Setting* setting = group->getSetting(index);
            if (!setting) {
                return nullptr;
            }
            return new PluginSetting(setting, true);
        }
    }
}