#pragma once

#include "Settings/ToySettings.h"
#include "ToyGroup.h"

#include "Core/Thread.h"

namespace Toys {
    class ToyThread {
    public:
        ToyThread(Threading::Thread* thread);

    private:
        static Settings::SynchronizationType getSynchronizationType(std::string& slot, Settings::ToySettings* globalSettings, Settings::SlotSettings* globalSlotSettings, Settings::ToySettings* toySettings);

        Threading::Thread* thread;
        std::vector<ToyGroup> groups;

        void loop();
        void peak(actionIndex action);
        void speedChanged();
        void nodeChanged();
        void threadEnd();
    };
}