#pragma once

#include "GameAPI/GameActor.h"
#include "Threading/Thread/Peak/PeakHandler.h"
#include "PeakTypeParams.h"

namespace Graph {
    namespace Action {
        namespace Peak {
            class PeakType {
            public:
                static PeakType* fromJson(std::string filename, json json);

                inline PeakType(PeakTypeParams params) {}

                virtual Threading::Threads::Peak::PeakHandler* create(Threading::Threads::Peak::PeakHandlerParams params, GameAPI::GameActor actor, GameAPI::GameActor target) = 0;
            };
        }
    }
}