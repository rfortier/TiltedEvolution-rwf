#include "ToyGroup.h"

namespace Toys {
    ToyGroup::ToyGroup(Graph::Action::Action* action, std::vector<std::tuple<Threading::ThreadActor*, std::string, ToyWrapper*>>& toys)
        : action{action}, hasPeaks{action->doPeaks && (action->peaksAnnotated || action->attributes->peakType)} {
        for (auto& [actor, slot, toy] : toys) {
            this->toys.push_back({actor, slot, toy});
        }

        for (ToyHandler& toy : this->toys) {
            toy.update(-1);
        }
    }

    void ToyGroup::loop() {
        timeSinceLastPeak += Constants::LOOP_TIME_MILLISECONDS;
        for (ToyHandler& toy : toys) {
            toy.loop();
        }

        if (hasPeaks) {
            return;
        }

        if (timeSinceLastPeak > 1000) {
            for (ToyHandler& toy : toys) {
                toy.update(-1);
            }

            timeSinceLastPeak = 0;
        }
    }

    void ToyGroup::peak() {
        if (nextPeakIndex >= peakIntervals.size()) {
            nextPeakIndex = 0;
            timeSinceLastPeak = 0;
            return;
        }

        peakIntervals[nextPeakIndex] = timeSinceLastPeak;
        ++nextPeakIndex %= 5;
        timeSinceLastPeak = 0;
        
        
        uint16_t sum = 0;
        uint16_t divisor = 0;
        for (int interval : peakIntervals) {
            if (interval == 0) {
                break;
            }

            sum += interval;
            divisor++;
        }

        uint16_t peakInterval = sum / divisor;

        for (ToyHandler& toy : toys) {
            toy.update(peakInterval);
        }
    }

    void ToyGroup::speedChanged() {
        nextPeakIndex = peakIntervals.size();
        for (uint16_t& peakInterval : peakIntervals) {
            peakInterval = 0;
        }
    }

    void ToyGroup::climax(Threading::ThreadActor* actor) {
        for (ToyHandler& toy : toys) {
            if (toy.isActor(actor)) {
                toy.climax();
            }
        }
    }

    void ToyGroup::stop() {
        for (ToyHandler& toy: toys) {
            toy.stop();
        }
    }
}