#pragma once

namespace Settings {
    namespace BaseTypes {
        struct CommonSettingParams {
            std::string name = "";
            std::string tooltip = "";
            bool alwaysRedraw = false;
            std::function<bool()> enabled = [] { return true; };
        };
    }
}