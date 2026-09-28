#pragma once

namespace Graph {
    namespace Action {
        namespace Peak {
            struct BoneDistancePeakTypeParams {
                bool inverse = false;
                std::vector<std::string> actorBones;
                std::vector<std::string> targetBones;
            };
        }
    }
}