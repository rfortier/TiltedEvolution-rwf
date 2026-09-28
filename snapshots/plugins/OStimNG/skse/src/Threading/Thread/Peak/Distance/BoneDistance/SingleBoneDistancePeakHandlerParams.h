#pragma once

#include "GameAPI/GameActorBone.h"

namespace Threading {
    namespace Threads {
        namespace Peak {
            struct SingleBoneDistancePeakHandlerParams {
                GameAPI::GameActorBone actorBone;
                GameAPI::GameActorBone targetBone;
            };
        }
    }
}