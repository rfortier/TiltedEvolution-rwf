#pragma once

#include "ToyWrapper.h"

#include "Settings/SlotSettings.h"

#include "Core/ThreadActor.h"

namespace Toys {
    class ToyHandler {
    public:
        ToyHandler(Threading::ThreadActor* actor, std::string slot, ToyWrapper* toy);

        inline bool isActor(Threading::ThreadActor* actor) { return this->actor == actor; }
        inline bool isSlot(std::string slot) { return this->slot == slot; }

        void loop();
        void update(uint16_t peakInterval);
        void climax();
        void stop();

    private:
        Threading::ThreadActor* actor;
        std::string slot;
        Settings::SlotSettings* settings;
        ToyWrapper* toy;
        uint16_t updateInterval;
        int16_t updateCooldown = 0;
    };
}