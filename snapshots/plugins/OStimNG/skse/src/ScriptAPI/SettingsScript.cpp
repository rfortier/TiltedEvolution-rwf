#include "SettingsScript.h"

#include "Settings/SettingTable.h"

namespace ScriptAPI {
    namespace Settings {
        template<typename T>
        T getPageProperty(int page, T fallback, std::function<T(::Settings::SettingPage*)> getter) {
            ::Settings::SettingPage* pageObject = ::Settings::SettingTable::getSingleton()->getPage(page);
            if (!pageObject) {
                return fallback;
            }

            return getter(pageObject);
        }

        template <typename T>
        T getGroupProperty(int page, int group, T fallback, std::function<T(::Settings::SettingGroup*)> getter) {
            ::Settings::SettingPage* pageObject = ::Settings::SettingTable::getSingleton()->getPage(page);
            if (!pageObject) {
                return fallback;
            }

            ::Settings::SettingGroup* groupObject = pageObject->getGroup(group);
            if (!groupObject) {
                return fallback;
            }

            T result = getter(groupObject);
            if (groupObject->isDisposable()) {
                delete groupObject;
            }
            return result;
        }

        template <typename T>
        T getSettingProperty(int page, int group, int setting, T fallback, std::function<T(::Settings::Setting*)> getter) {
            ::Settings::SettingPage* pageObject = ::Settings::SettingTable::getSingleton()->getPage(page);
            if (!pageObject) {
                return fallback;
            }

            ::Settings::SettingGroup* groupObject = pageObject->getGroup(group);
            if (!groupObject) {
                return fallback;
            }

            ::Settings::Setting* settingObject = groupObject->getSetting(setting);
            if (groupObject->isDisposable()) {
                delete groupObject;
            }
            if (!settingObject) {
                return fallback;
            }

            T result = getter(settingObject);
            if (settingObject->isDisposable()) {
                delete settingObject;
            }
            return result;
        }



        void menuOpened() {
            ::Settings::SettingTable::getSingleton()->menuOpenend();
        }
        
        
        int getSettingPageCount() {
            return ::Settings::SettingTable::getSingleton()->getPageCount();
        }

        std::string getSettingPageName(int page) {
            return getPageProperty<std::string>(page, "", [](::Settings::SettingPage* page) { return page->getName(); });
        }

        int getSettingPageDisplayOrder(int page) {
            return getPageProperty<int>(page, -1, [](::Settings::SettingPage* page) { return static_cast<int>(page->getDisplayOrder()); });
        }


        int getSettingGroupCount(int page) {
            return getPageProperty<int>(page, -1, [](::Settings::SettingPage* page) { return static_cast<int>(page->getGroupCount()); });
        }

        std::string getSettingGroupName(int page, int group) {
            return getGroupProperty<std::string>(page, group, "", [](::Settings::SettingGroup* group) { return group->getName(); });
        }

        int getSettingGroupDisplayOrder(int page, int group) {
            return getGroupProperty<int>(page, group, -1, [](::Settings::SettingGroup* group) { return static_cast<int>(group->getDisplayOrder()); });
        }


        int getSettingCount(int page, int group) {
            return getGroupProperty<int>(page, group, -1, [](::Settings::SettingGroup* group) { return group->getSettingCount(); });
        }

        std::string getSettingName(int page, int group, int setting) {
            return getSettingProperty<std::string>(page, group, setting, "", [](::Settings::Setting* setting) { return setting->getName(); });
        }

        std::string getSettingTooltip(int page, int group, int setting) {
            return getSettingProperty<std::string>(page, group, setting, "", [](::Settings::Setting* setting) { return setting->getTooltip(); });
        }

        int getSettingType(int page, int group, int setting) {
            return getSettingProperty<int>(page, group, setting, -1, [](::Settings::Setting* setting) { return static_cast<int>(setting->getType()); });
        }

        bool isSettingEnabled(int page, int group, int setting) {
            return getSettingProperty<bool>(page, group, setting, false, [](::Settings::Setting* setting) { return setting->isEnabled(); });
        }


        bool isSettingActivatedByDefault(int page, int group, int setting) {
            return getSettingProperty<bool>(page, group, setting, false, [](::Settings::Setting* setting) { return setting->isActivatedByDefault(); });
        }

        bool isSettingActivated(int page, int group, int setting) {
            return getSettingProperty<bool>(page, group, setting, false, [](::Settings::Setting* setting) { return setting->isActivated(); });
        }

        bool toggleSetting(int page, int group, int setting) {
            return getSettingProperty<bool>(page, group, setting, false, [](::Settings::Setting* setting) { return setting->toggle(); });
        }


        float getDefaultSettingValue(int page, int group, int setting) {
            return getSettingProperty<float>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getDefaultValue(); });
        }

        float getCurrentSettingValue(int page, int group, int setting) {
            return getSettingProperty<float>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getCurrentValue(); });
        }

        float getSettingValueStep(int page, int group, int setting) {
            return getSettingProperty<float>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getValueStep(); });
        }

        float getMinSettingValue(int page, int group, int setting) {
            return getSettingProperty<float>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getMinValue(); });
        }

        float getMaxSettingValue(int page, int group, int setting) {
            return getSettingProperty<float>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getMaxValue(); });
        }

        bool setSettingValue(int page, int group, int setting, float value) {
            return getSettingProperty<bool>(page, group, setting, false, [value](::Settings::Setting* setting) { return setting->setValue(value); });
        }


        int getDefaultSettingIndex(int page, int group, int setting) {
            return getSettingProperty<int>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getDefaultIndex(); });
        }

        int getCurrentSettingIndex(int page, int group, int setting) {
            return getSettingProperty<int>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getCurrentIndex(); });
        }

        std::string getCurrentSettingOption(int page, int group, int setting) {
            return getSettingProperty<std::string>(page, group, setting, "", [](::Settings::Setting* setting) { return setting->getCurrentOption(); });
        }

        std::vector<std::string> getSettingOptions(int page, int group, int setting) {
            return getSettingProperty<std::vector<std::string>>(page, group, setting, {}, [](::Settings::Setting* setting) { return setting->getOptions(); });
        }

        bool setSettingIndex(int page, int group, int setting, int index) {
            return getSettingProperty<bool>(page, group, setting, false, [index](::Settings::Setting* setting) { return setting->setIndex(index); });
        }


        std::string getDefaultSettingText(int page, int group, int setting) {
            return getSettingProperty<std::string>(page, group, setting, "", [](::Settings::Setting* setting) { return setting->getDefaultText(); });
        }

        std::string getCurrentSettingText(int page, int group, int setting) {
            return getSettingProperty<std::string>(page, group, setting, "", [](::Settings::Setting* setting) { return setting->getCurrentText(); });
        }

        bool setSettingText(int page, int group, int setting, std::string text) {
            return getSettingProperty<bool>(page, group, setting, false, [text](::Settings::Setting* setting) { return setting->setText(text); });
        }


        int getDefaultSettingKey(int page, int group, int setting) {
            return getSettingProperty<int>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getDefaultKey(); });
        }

        int getCurrentSettingKey(int page, int group, int setting) {
            return getSettingProperty<int>(page, group, setting, 0, [](::Settings::Setting* setting) { return setting->getCurrentKey(); });
        }

        bool setSettingKey(int page, int group, int setting, int key) {
            return getSettingProperty<bool>(page, group, setting, false, [key](::Settings::Setting* setting) { return setting->setKey(key); });
        }


        bool clickSetting(int page, int group, int setting) {
            return getSettingProperty<bool>(page, group, setting, false, [](::Settings::Setting* setting) { return setting->click(); });
        }
    }
}