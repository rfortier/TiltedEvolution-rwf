#pragma once

#include "ScriptAPI/SettingsScript.h"

namespace PapyrusSettings {
    using VM = RE::BSScript::IVirtualMachine;


    void MenuOpened(RE::StaticFunctionTag*) {
        ScriptAPI::Settings::menuOpened();
    }


    int GetSettingPageCount(RE::StaticFunctionTag*) {
        return ScriptAPI::Settings::getSettingPageCount();
    }

    std::string GetSettingPageName(RE::StaticFunctionTag*, int page) {
        return ScriptAPI::Settings::getSettingPageName(page);
    }

    int GetSettingPageDisplayOrder(RE::StaticFunctionTag*, int page) {
        return ScriptAPI::Settings::getSettingPageDisplayOrder(page);
    }


    int GetSettingGroupCount(RE::StaticFunctionTag*, int page) {
        return ScriptAPI::Settings::getSettingGroupCount(page);
    }

    std::string GetSettingGroupName(RE::StaticFunctionTag*, int page, int group) {
        return ScriptAPI::Settings::getSettingGroupName(page, group);
    }

    int GetSettingGroupDisplayOrder(RE::StaticFunctionTag*, int page, int group) {
        return ScriptAPI::Settings::getSettingGroupDisplayOrder(page, group);
    }


    int GetSettingCount(RE::StaticFunctionTag*, int page, int group) {
        return ScriptAPI::Settings::getSettingCount(page, group);
    }

    std::string GetSettingName(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getSettingName(page, group, setting);
    }

    std::string GetSettingTooltip(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getSettingTooltip(page, group, setting);
    }

    int GetSettingType(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getSettingType(page, group, setting);
    }

    bool IsSettingEnabled(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::isSettingEnabled(page, group, setting);
    }


    bool IsSettingActivatedByDefault(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::isSettingActivatedByDefault(page, group, setting);
    }

    bool IsSettingActivated(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::isSettingActivated(page, group, setting);
    }

    bool ToggleSetting(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::toggleSetting(page, group, setting);
    }


    float GetDefaultSettingValue(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getDefaultSettingValue(page, group, setting);
    }

    float GetCurrentSettingValue(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getCurrentSettingValue(page, group, setting);
    }

    float GetSettingValueStep(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getSettingValueStep(page, group, setting);
    }

    float GetMinSettingValue(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getMinSettingValue(page, group, setting);
    }

    float GetMaxSettingValue(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getMaxSettingValue(page, group, setting);
    }

    bool SetSettingValue(RE::StaticFunctionTag*, int page, int group, int setting, float value) {
        return ScriptAPI::Settings::setSettingValue(page, group, setting, value);
    }


    int GetDefaultSettingIndex(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getDefaultSettingIndex(page, group, setting);
    }

    int GetCurrentSettingIndex(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getCurrentSettingIndex(page, group, setting);
    }

    std::string GetCurrentSettingOption(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getCurrentSettingOption(page, group, setting);
    }

    std::vector<std::string> GetSettingOptions(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getSettingOptions(page, group, setting);
    }

    bool SetSettingIndex(RE::StaticFunctionTag*, int page, int group, int setting, int index) {
        return ScriptAPI::Settings::setSettingIndex(page, group, setting, index);
    }


    std::string GetDefaultSettingText(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getDefaultSettingText(page, group, setting);
    }

    std::string GetCurrentSettingText(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getCurrentSettingText(page, group, setting);
    }

    bool SetSettingText(RE::StaticFunctionTag*, int page, int group, int setting, std::string text) {
        return ScriptAPI::Settings::setSettingText(page, group, setting, text);
    }


    int GetDefaultSettingKey(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getDefaultSettingKey(page, group, setting);
    }

    int GetCurrentSettingKey(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::getCurrentSettingKey(page, group, setting);
    }

    bool SetSettingKey(RE::StaticFunctionTag*, int page, int group, int setting, int key) {
        return ScriptAPI::Settings::setSettingKey(page, group, setting, key);
    }


    bool ClickSetting(RE::StaticFunctionTag*, int page, int group, int setting) {
        return ScriptAPI::Settings::clickSetting(page, group, setting);
    }


    bool Bind(VM* a_vm) {
        const auto obj = "OSettings"sv;

        BIND(MenuOpened);

        BIND(GetSettingPageCount);
        BIND(GetSettingPageName);
        BIND(GetSettingPageDisplayOrder);

        BIND(GetSettingGroupCount);
        BIND(GetSettingGroupName);
        BIND(GetSettingGroupDisplayOrder);

        BIND(GetSettingCount);
        BIND(GetSettingName);
        BIND(GetSettingTooltip);
        BIND(GetSettingType);
        BIND(IsSettingEnabled);

        BIND(IsSettingActivatedByDefault);
        BIND(IsSettingActivated);
        BIND(ToggleSetting);

        BIND(GetDefaultSettingValue);
        BIND(GetCurrentSettingValue);
        BIND(GetSettingValueStep);
        BIND(GetMinSettingValue);
        BIND(GetMaxSettingValue);
        BIND(SetSettingValue);

        BIND(GetDefaultSettingIndex);
        BIND(GetCurrentSettingIndex);
        BIND(GetCurrentSettingOption);
        BIND(GetSettingOptions);
        BIND(SetSettingIndex);

        BIND(GetDefaultSettingText);
        BIND(GetCurrentSettingText);
        BIND(SetSettingText);

        BIND(GetDefaultSettingKey);
        BIND(GetCurrentSettingKey);
        BIND(SetSettingKey);

        BIND(ClickSetting);

        return true;
    }
}