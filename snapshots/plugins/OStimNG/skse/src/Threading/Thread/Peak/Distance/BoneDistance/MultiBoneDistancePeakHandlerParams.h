#pragma once

#include "GameAPI/GameActorBone.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            struct MultiBoneDistancePeakHandlerParams {
                std::vector<GameAPI::GameActorBone> actorBones;
                std::vector<GameAPI::GameActorBone> targetBones;
            };
        }
    }
}