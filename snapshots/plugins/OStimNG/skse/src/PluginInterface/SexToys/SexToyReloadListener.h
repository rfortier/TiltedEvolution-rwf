#pragma once

namespace OStim {
    class SexToyReloadListener {
    public:
        /**
         * gets called once on save load and again every time the player thread starts
         * all previously registered toys will be wiped right before this gets called
         * so always register all your toys whenever this gets called
         * note: OStim does not delete the old ones from memory, that is your resposibility
         * 
         * gets called from detached threads so you don't have to worry about freezing the game
         * still it shouldn't take ten seconds for toys to set up after a scene starts
         */
        virtual void reloadToys() = 0;
    };
}