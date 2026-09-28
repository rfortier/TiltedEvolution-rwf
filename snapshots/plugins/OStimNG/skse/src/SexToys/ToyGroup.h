#pragma once

#include "ToyHandler.h"

#include "Graph/Action/Action.h"

namespace Toys {
    class ToyGroup {
    public:
        ToyGroup(Graph::Action::Action* action, std::vector<std::tuple<Threading::ThreadActor*, std::string, ToyWrapper*>>& toys);

        inline bool isAction(actionIndex action) { return this->action->index == action; }

        void loop();
        void peak();
        void speedChanged();
        void climax(Threading::ThreadActor* actor);
        void stop();

    private:
        std::vector<ToyHandler> toys;
        Graph::Action::Action* action;
        bool hasPeaks;
        uint16_t timeSinceLastPeak = 0;
        uint8_t nextPeakIndex = 5;
        std::vector<uint16_t> peakIntervals = {0, 0, 0, 0, 0};
    };
}