#include "ToySettingMenu.h"

#include "../Settings/Settings.h"
#include "../ToyTable.h"

#include "Settings/Implementation/Lambda/AdvancedLambdaDropDownSetting.h"
#include "Settings/Implementation/Lambda/SimpleLambdaDropDownSetting.h"
#include "Settings/Implementation/Pointer/PointerProviderMinMaxSliderSetting.h"
#include "Settings/Implementation/Pointer/PointerProviderSliderSetting.h"
#include "Settings/Implementation/Pointer/PointerProviderToggleSetting.h"
#include "Settings/Implementation/SimpleSettingGroup.h"

namespace Toys {
    namespace Menu {
        ToySettingMenu::ToySettingMenu() {
            std::vector<::Settings::Setting*> toySettings;
            toySettings.push_back(new ::Settings::LambdaSettings::AdvancedLambdaDropDownSetting({"$ostim_select_toy", "$ostim_tooltip_select_toy", true}, 0, [&](){ return static_cast<dropDownIndex>(toy); }, [&](dropDownIndex index){ selectToy(index); }, [&](){ return static_cast<dropDownIndex>(toys.size()); },[&](dropDownIndex index){ return toys[index].name; }));
            toySettings.push_back(new ::Settings::LambdaSettings::AdvancedLambdaDropDownSetting({"$ostim_select_slot", "$ostim_tooltip_select_slot", true}, 0, [&](){ return static_cast<dropDownIndex>(slot); }, [&](dropDownIndex index){ selectSlot(index); }, [&](){ return static_cast<dropDownIndex>(slots.size()); },[&](dropDownIndex index){ return slots[index]; }));
            toySettings.push_back(new ::Settings::PointerSettings::PointerProviderToggleSetting({"$ostim_toy_slot_enable", "$ostim_tooltip_toy_slot_enable"}, {Settings::SlotSettings::enabledDefault}, [&]() { return &slotSettings->enabled; }));
            toySettingGroups.push_back(new ::Settings::SimpleSettingGroup("$ostim_header_toys", getDisplayOrder(), toySettings));

            std::vector<::Settings::Setting*> synchronizationSettings;
            synchronizationSettings.push_back(new ::Settings::LambdaSettings::SimpleLambdaDropDownSetting({"$ostim_toy_slot_synchronization_type", "$ostim_tooltip_toy_slot_synchronization_type"}, {Settings::SynchronizationSettings::synchronizationTypeDefault, Settings::SynchronizationTypeUtil::getStringValues()}, [&](){ return static_cast<dropDownIndex>(slotSettings->synchronizationSettings.synchronizationType);}, [&](dropDownIndex index){ slotSettings->synchronizationSettings.synchronizationType = static_cast<Settings::SynchronizationType>(index);}));
            toySettingGroups.push_back(new ::Settings::SimpleSettingGroup("$ostim_header_toys_synchronization", getDisplayOrder(), synchronizationSettings));

            std::vector<::Settings::Setting*> scalingSettings;
            scalingSettings.push_back(new ::Settings::LambdaSettings::SimpleLambdaDropDownSetting({"$ostim_toy_slot_scaling_type", "$ostim_tooltip_toy_slot_scaling_type"}, {Settings::ScalingSettings::scalingTypeDefault, Settings::ScalingTypeUtil::getStringValues()}, [&](){ return static_cast<dropDownIndex>(slotSettings->scalingSettings.scalingType); }, [&](dropDownIndex index){ slotSettings->scalingSettings.scalingType = static_cast<Settings::ScalingType>(index); }));
            scalingSettings.push_back(new ::Settings::PointerSettings::PointerProviderMinMaxSliderSetting({"$ostim_toy_slot_min_scale", "$ostim_tooltip_toy_slot_min_scale"}, {Settings::ScalingSettings::minScaleDefault, 0.05f, 0.0f, 1.0f}, {true}, [&]() { return &slotSettings->scalingSettings.minScale; }, [&]() { return &slotSettings->scalingSettings.maxScale; }));
            scalingSettings.push_back(new ::Settings::PointerSettings::PointerProviderMinMaxSliderSetting({"$ostim_toy_slot_max_scale", "$ostim_tooltip_toy_slot_max_scale"}, {Settings::ScalingSettings::maxScaleDefault, 0.05f, 0.0f, 1.0f}, {false}, [&]() { return &slotSettings->scalingSettings.minScale; }, [&]() { return &slotSettings->scalingSettings.maxScale; }));
            scalingSettings.push_back(new ::Settings::PointerSettings::PointerProviderMinMaxSliderSetting({"$ostim_toy_slot_min_magnitude", "$ostim_tooltip_toy_slot_min_magnitude"}, {Settings::ScalingSettings::minMagnitudeDefault, 0.05f, 0.0f, 1.0f}, {true}, [&]() { return &slotSettings->scalingSettings.minMagnitude; }, [&]() { return &slotSettings->scalingSettings.maxMagnitude; }));
            scalingSettings.push_back(new ::Settings::PointerSettings::PointerProviderMinMaxSliderSetting({"$ostim_toy_slot_max_magnitude", "$ostim_tooltip_toy_slot_max_magnitude"}, {Settings::ScalingSettings::maxMagnitudeDefault, 0.05f, 0.0f, 1.0f}, {false}, [&]() { return &slotSettings->scalingSettings.minMagnitude; }, [&]() { return &slotSettings->scalingSettings.maxMagnitude; }));
            scalingSettings.push_back(new ::Settings::PointerSettings::PointerProviderToggleSetting({"$ostim_toy_slot_do_peaks", "$ostim_tooltip_toy_slot_do_peaks"}, {Settings::ScalingSettings::doPeaksDefault}, [&]() { return &slotSettings->scalingSettings.doPeaks; }));
            scalingSettings.push_back(new ::Settings::PointerSettings::PointerProviderMinMaxSliderSetting({"$ostim_toy_slot_min_peak", "$ostim_tooltip_toy_slot_min_peak"}, {Settings::ScalingSettings::minPeakDefault, 0.05f, 0.0f, 1.0f}, {true}, [&]() { return &slotSettings->scalingSettings.minPeak; }, [&]() { return &slotSettings->scalingSettings.maxPeak; }));
            scalingSettings.push_back(new ::Settings::PointerSettings::PointerProviderMinMaxSliderSetting({"$ostim_toy_slot_max_peak", "$ostim_tooltip_toy_slot_max_peak"}, {Settings::ScalingSettings::maxPeakDefault, 0.05f, 0.0f, 1.0f}, {false}, [&]() { return &slotSettings->scalingSettings.minPeak; }, [&]() { return &slotSettings->scalingSettings.maxPeak; }));
            toySettingGroups.push_back(new ::Settings::SimpleSettingGroup("$ostim_header_toys_scaling", getDisplayOrder(), scalingSettings));

            std::vector<::Settings::Setting*> climaxSettings;
            climaxSettings.push_back(new ::Settings::PointerSettings::PointerProviderToggleSetting({"$ostim_toy_slot_do_climax", "$ostim_tooltip_toy_slot_do_climax"}, {Settings::ClimaxSettings::doClimaxDefault}, [&]() { return &slotSettings->climaxSettings.doClimax; }));
            climaxSettings.push_back(new ::Settings::PointerSettings::PointerProviderSliderSetting({"$ostim_toy_slot_climax_magnitude", "$ostim_tooltip_toy_slot_climax_magnitude"}, {Settings::ClimaxSettings::climaxMagnitudeDefault, 0.05f, 0.0f, 1.0f}, [&]() { return &slotSettings->climaxSettings.climaxMagnitude; }));
            toySettingGroups.push_back(new ::Settings::SimpleSettingGroup("$ostim_header_toys_climax", getDisplayOrder(), climaxSettings));
        }

        void ToySettingMenu::onMenuOpened() {
            toys.clear();
            toys.push_back({"all", "all"});
            for (ToyWrapper& toy : *ToyTable::getSingleton()->getToys()) {
                toys.push_back({toy.getID(), toy.getName()});
            }
            std::sort(toys.begin(), toys.end(), [](ToyInfo a, ToyInfo b) { return b.id != "all" && (a.id == "all" || a.name < b.name); });

            slots.clear();
            slots.push_back("all");
            for (const std::string& slot : ToyTable::getSingleton()->getToySlots()){
                slots.push_back(slot);
            }
            std::sort(slots.begin(), slots.end(), [](std::string a, std::string b) { return b != "all" && (a == "all" || a < b); });

            selectToy(defaultToy);
            selectSlot(defaultSlot);
        }

        std::string ToySettingMenu::getName() {
            return "$ostim_page_toys";
        }

        ::Settings::SettingDisplayOrder ToySettingMenu::getDisplayOrder() {
            return ::Settings::SettingDisplayOrder::TOP_TO_BOTTOM;
        }

        settingGroupIndex ToySettingMenu::getGroupCount() {
            return toySettingGroups.size();
        }

        ::Settings::SettingGroup* ToySettingMenu::getGroup(settingGroupIndex index) {
            if (index < 0 || index >= toySettingGroups.size()) {
                return nullptr;
            }
            return toySettingGroups[index];
        }


        void ToySettingMenu::selectToy(int toy) {
            if (toy < 0 || toy >= toys.size()) {
                return;
            }

            this->toy = toy;
            loadSettings();
        }

        void ToySettingMenu::selectSlot(int slot) {
            if (slot < 0 || slot >= slots.size()) {
                return;
            }

            this->slot = slot;
            loadSettings();
        }

        void ToySettingMenu::loadSettings() {
            slotSettings = Settings::Settings::getSingleton()->getToySettings(toys[toy].id)->getSlotSettings(slots[slot]);
        }
    }
}