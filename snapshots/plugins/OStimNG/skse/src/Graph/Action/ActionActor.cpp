#include "ActionActor.h"

#include "Util/VectorUtil.h"

namespace Graph {
    namespace Action {
        void ActionActor::merge(ActionActor& other) {
            if (other.fullStrip) {
                fullStrip = true;
            }

            if (other.moan) {
                moan = true;
            }

            if (other.talk) {
                talk = true;
            }

            if (other.muffled) {
                muffled = true;
            }

            for (std::string requirement : other.requirements) {
                requirements.insert(requirement);
            }

            strippingMask |= other.strippingMask;

            for (std::string equipObject : other.equipObjects) {
                equipObjects.insert(equipObject);
            }


            VectorUtil::mergeDistinct(factions, other.factions);
            VectorUtil::mergeDistinct(statFactions, other.statFactions);
            VectorUtil::mergeDistinct(playerStatFactions, other.playerStatFactions);
            VectorUtil::mergeDistinct(climaxStatFactions, other.climaxStatFactions);
            VectorUtil::mergeDistinct(partnerClimaxStatFactions, other.partnerClimaxStatFactions);
            VectorUtil::mergeDistinct(playerClimaxStatFactions, other.playerClimaxStatFactions);
            VectorUtil::mergeDistinct(playerPartnerClimaxStatFactions, other.playerPartnerClimaxStatFactions);
            VectorUtil::mergeDistinct(playerStatLists, other.playerStatLists);
            VectorUtil::mergeDistinct(playerClimaxStatLists, other.playerClimaxStatLists);
            VectorUtil::mergeDistinct(playerPartnerClimaxStatLists, other.playerPartnerClimaxStatLists);


            for (std::string& slot : other.toySlots) {
                if (!VectorUtil::contains(toySlots, slot)) {
                    toySlots.push_back(slot);
                }
            }
        }


        void* ActionActor::getTimesPerformedFaction() { return displayStatFaction.toABIPointer(); }
        void* ActionActor::getTimesPerformedWithPlayerFaction() { return displayPlayerStatFaction.toABIPointer(); }
        void* ActionActor::getTimesClimaxedFaction() { return displayClimaxStatFaction.toABIPointer(); }
        void* ActionActor::getTimesPartnerClimaxedFaction() { return displayPartnerClimaxStatFaction.toABIPointer(); }
        void* ActionActor::getTimesClimaxedWithPlayerFaction() { return displayPlayerClimaxStatFaction.toABIPointer(); }
        void* ActionActor::getTimesPartnerClimaxedWithPlayerFaction() { return displayPlayerPartnerClimaxStatFaction.toABIPointer(); }

        void* ActionActor::getPlayerMateList() { return displayPlayerStatList.toABIPointer(); }
        void* ActionActor::getPlayerMateClimaxedList() { return displayPlayerClimaxStatList.toABIPointer(); }
        void* ActionActor::getPlayerMatePartnerClimaxedList() { return displayPlayerPartnerClimaxStatList.toABIPointer(); }
    }
}