#pragma once

namespace OStim {
    class SexToy {
    public:
        /**
        * return the toys unique identifier
        * this will get used to serialize toy settings
        */
        virtual const char* getID() = 0;

        /**
        * return the toys display name
        * this will show up in the user interface
        */
        virtual const char* getName() = 0;

        /**
         * number of milliseconds that have to pass before another call to update is made
         * updates are always send when a peak happens (unless peaks are disabled)
         * if this returns 0 an update is send for every peak
         * if peaks are disabled an update is attempted every second
         */
        virtual uint16_t getUpdateInterval() = 0;

        // called on scene start/end
        // gets called from detached threads
        virtual void turnOn() = 0;
        virtual void turnOff() = 0;

        /**
         * an update is always send when a peak happens (unless peaks are disabled)
         * if the update interval is greater than zero it is send on the first peak after the cooldown
         * gets called from detached threads so you don't have to worry about freezing the game
         *
         * @param baseline: value between 0.0f and 1.0f for the strength of the toy between peaks
         * @param peak: value between 0.0f and 1.0f for the strength of the toy for peaks, this value is properly filled even if peaks are disabled
         * @param peakInterval: amount of milliseconds between peaks, if peaks are disabled value will be -1, value is estimated based on previous peak timings
         */
        virtual void update(float baseline, float peak, int16_t peakInterval) = 0;

        /**
         * called when the actor climaxes or other events are send
         * this should completely overwrite any previous commands
         * gets called from detached threads so you don't have to worry about freezing the game
         */
        virtual void peak(float magnitude, float duration) = 0;

        /**
         * immediately abort the latest update
         * this is usually called on animation change for toys that aren't in use in the new one
         * gets called from detached threads so you don't have to worry about freezing the game
        */
        virtual void stop() = 0;
    };
}  // namespace Toys