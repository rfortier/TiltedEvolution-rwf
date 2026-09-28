#pragma once

namespace ScriptAPI {
    namespace Metadata {
        std::string getName(std::string id);
        std::vector<std::string> nodesToNames(std::vector<std::string> ids);
    }
}