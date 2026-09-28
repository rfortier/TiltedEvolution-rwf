#pragma once

#include "Model/SettingPage.h"

#include "GameAPI/GameVariable.h"
#include "Util/Singleton.h"

namespace Settings {
    class AddonPage : public SettingPage, public Util::Singleton<AddonPage> {
    public:
        inline void addSkseGroup(SettingGroup* group) { skseGroups.push_back(group); }

        void loadJsonAddons();

        virtual void onMenuOpened();
        virtual std::string getName();
        virtual SettingDisplayOrder getDisplayOrder();

        virtual settingGroupIndex getGroupCount();
        virtual SettingGroup* getGroup(settingGroupIndex index);

        void writeJson(json& json);
        void readJson(json& json);

    private:
        std::vector<SettingGroup*> skseGroups;
        std::vector<SettingGroup*> jsonGroups;
        std::vector<GameAPI::GameVariable> jsonVariables;
    };
}