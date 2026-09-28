#include "DropDownSetting.h"

namespace Settings {
    namespace BaseTypes {
        DropDownSetting::DropDownSetting(CommonSettingParams params, DropDownSettingParams dropDownParams)
            : BaseSetting(params, SettingType::DROP_DOWN, dropDownParams.defaultIndex),
              options{dropDownParams.options} {}

        std::string DropDownSetting::getCurrentOption() {
            int currentIndex = getCurrentIndex();
            if (currentIndex < 0 || currentIndex >= options.size()) {
                return "";
            }
            return options[currentIndex];
        }

        std::vector<std::string> DropDownSetting::getOptions() {
            return options;
        }
    }
}