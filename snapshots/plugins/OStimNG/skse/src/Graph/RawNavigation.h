#pragma once

namespace Graph {
    struct RawNavigation {
        int priority = 0;
        std::string origin = "";
        std::string destination = "";
        std::string description = "";
        std::string icon = "";
        std::string border = "ffffff";
        bool noWarnings = false;
    };
}