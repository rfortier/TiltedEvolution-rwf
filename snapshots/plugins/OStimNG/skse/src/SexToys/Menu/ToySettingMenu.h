#pragma once

#include "../Settings/SlotSettings.h"
#include "ToyInfo.h"

#include "Settings/Model/SettingPage.h"
#include "Util/Singleton.h"

namespace Toys {
    namespace Menu {
        class ToySettingMenu : public ::Settings::SettingPage, public Util::Singleton<ToySettingMenu> {
        public:
            ToySettingMenu();

            virtual void onMenuOpened() override;
            virtual std::string getName() override;
            virtual ::Settings::SettingDisplayOrder getDisplayOrder() override;

            virtual settingGroupIndex getGroupCount() override;
            virtual ::Settings::SettingGroup* getGroup(settingGroupIndex index) override;

        private:
            inline static int defaultToy = 0;
            inline static int defaultSlot = 0;

            std::vector<ToyInfo> toys;
            std::vector<std::string> slots;

            int toy = defaultToy;
            int slot = defaultSlot;
            Settings::SlotSettings* slotSettings = nullptr;

            std::vector<::Settings::SettingGroup*> toySettingGroups;

            void selectToy(int toy);
            void selectSlot(int slot);
            void loadSettings();
        };
    }
}