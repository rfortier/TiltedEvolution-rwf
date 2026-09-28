#pragma once

namespace ScriptAPI {
    namespace Settings {
        void menuOpened();

        int getSettingPageCount();
        std::string getSettingPageName(int page);
        int getSettingPageDisplayOrder(int page);

        int getSettingGroupCount(int page);
        std::string getSettingGroupName(int page, int group);
        int getSettingGroupDisplayOrder(int page, int group);

        int getSettingCount(int page, int group);
        std::string getSettingName(int page, int group, int setting);
        std::string getSettingTooltip(int page, int group, int setting);
        int getSettingType(int page, int group, int setting);
        bool isSettingEnabled(int page, int group, int setting);

        bool isSettingActivatedByDefault(int page, int group, int setting);
        bool isSettingActivated(int page, int group, int setting);
        bool toggleSetting(int page, int group, int setting);

        float getDefaultSettingValue(int page, int group, int setting);
        float getCurrentSettingValue(int page, int group, int setting);
        float getSettingValueStep(int page, int group, int setting);
        float getMinSettingValue(int page, int group, int setting);
        float getMaxSettingValue(int page, int group, int setting);
        bool setSettingValue(int page, int group, int setting, float value);

        int getDefaultSettingIndex(int page, int group, int setting);
        int getCurrentSettingIndex(int page, int group, int setting);
        std::string getCurrentSettingOption(int page, int group, int setting);
        std::vector<std::string> getSettingOptions(int page, int group, int setting);
        bool setSettingIndex(int page, int group, int setting, int index);

        std::string getDefaultSettingText(int page, int group, int setting);
        std::string getCurrentSettingText(int page, int group, int setting);
        bool setSettingText(int page, int group, int setting, std::string text);

        int getDefaultSettingKey(int page, int group, int setting);
        int getCurrentSettingKey(int page, int group, int setting);
        bool setSettingKey(int page, int group, int setting, int key);

        bool clickSetting(int page, int group, int Setting);
    }
}