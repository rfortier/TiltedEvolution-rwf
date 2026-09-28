#pragma once

#include "../BaseTypes/ToggleSetting.h"

namespace Settings {
    namespace PointerSettings {
        class PointerProviderToggleSetting : public BaseTypes::ToggleSetting {
        public:
            PointerProviderToggleSetting(BaseTypes::CommonSettingParams params, BaseTypes::ToggleSettingParams toggleParams, std::function<bool*()> valueProvider);

            virtual bool isActivated();
            virtual bool toggle();

        private:
            std::function<bool*()> valueProvider;
        };
    }
}