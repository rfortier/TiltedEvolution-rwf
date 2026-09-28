#pragma once

#include "BaseSetting.h"
#include "DropDownSettingParams.h"

namespace Settings {
    namespace BaseTypes {
        class DropDownSetting : public BaseSetting {
        public:
            DropDownSetting(CommonSettingParams params, DropDownSettingParams dropDownParams);

            virtual std::string getCurrentOption();
            virtual std::vector<std::string> getOptions();

        private:
            std::vector<std::string> options;
        };
    }
}