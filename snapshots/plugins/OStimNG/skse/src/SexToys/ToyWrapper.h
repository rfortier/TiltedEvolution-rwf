#pragma once

#include "PluginInterface/SexToys/SexToy.h"

namespace Toys {
    class ToyWrapper {
    public:
        ToyWrapper(OStim::SexToy* toy);

        inline std::string getID() { return id; }
        inline std::string getName() { return name; }
        inline uint16_t getUpdateInterval() { return toy->getUpdateInterval(); }

        void turnOn();
        void turnOff(bool overwrite);
        inline void turnOff() { turnOff(false); }

        void update(float baseline, float peak, int16_t peakInterval);
        void peak(float magnitude, float duration);
        void stop();

    private:
        OStim::SexToy* toy;
        std::string id;
        std::string name;

        int8_t users = 0; // need signed version as negative numbers resemble a hard stop
    };
}