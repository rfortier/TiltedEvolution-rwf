#include "PointerProviderToggleSetting.h"

namespace Settings {
    namespace PointerSettings {
        PointerProviderToggleSetting::PointerProviderToggleSetting(BaseTypes::CommonSettingParams params, BaseTypes::ToggleSettingParams toggleParams, std::function<bool*()> valueProvider)
            : BaseTypes::ToggleSetting(params, toggleParams),
              valueProvider{valueProvider} {}

        bool PointerProviderToggleSetting::isActivated() {
            return *valueProvider();
        }

        bool PointerProviderToggleSetting::toggle() {
            *valueProvider() = !*valueProvider();
            return shouldRedraw();
        }
    }
}