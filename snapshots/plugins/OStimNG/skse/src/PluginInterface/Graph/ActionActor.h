#pragma once

namespace OStim {
    class ActionActor {
    public:
        // --- ABI version 0x07050003 ---

        // stat factions: all faction ranks stop increasing at 101
        // note: all of these may return nullpointers

        // faction for how many times the actor performed this
        virtual void* getTimesPerformedFaction() = 0;
        // faction for how many times the actor performed this with the player
        virtual void* getTimesPerformedWithPlayerFaction() = 0;
        // faction for how many times the actor climaxed while performing this
        virtual void* getTimesClimaxedFaction() = 0;
        // faction for how many times the actors partner climaxed while performing this
        virtual void* getTimesPartnerClimaxedFaction() = 0;
        // faction for how many times the actor climaxed while performing this with the player
        virtual void* getTimesClimaxedWithPlayerFaction() = 0;
        // faction for how many times the actor made the PC climax while performing this
        virtual void* getTimesPartnerClimaxedWithPlayerFaction() = 0;

        // list of all actors who have performed this with the PC
        virtual void* getPlayerMateList() = 0;
        // list of all actors who had a climax while performing this with the PC
        virtual void* getPlayerMateClimaxedList() = 0;
        // list of all actors who made the PC climax while performing this
        virtual void* getPlayerMatePartnerClimaxedList() = 0;
    };
}