#include "GraphActor.h"

#include "Util/StringUtil.h"
#include "Util/VectorUtil.h"

namespace Graph {
    void GraphActor::merge(Action::ActionActor& actor) {
        moan |= actor.moan;
        talk |= actor.talk;
        muffled |= actor.muffled;
        for (const std::string& requirement : actor.requirements) {
            condition.requirements.insert(requirement);
        }
        for (const std::string& equipObject : actor.equipObjects) {
            equipObjects.insert(equipObject);
        }

        VectorUtil::mergeDistinct(factions, actor.factions);
        VectorUtil::mergeDistinct(statFactions, actor.statFactions);
        VectorUtil::mergeDistinct(playerStatFactions, actor.playerStatFactions);
        VectorUtil::mergeDistinct(climaxStatFactions, actor.climaxStatFactions);
        VectorUtil::mergeDistinct(partnerClimaxStatFactions, actor.partnerClimaxStatFactions);
        VectorUtil::mergeDistinct(playerClimaxStatFactions, actor.playerClimaxStatFactions);
        VectorUtil::mergeDistinct(playerPartnerClimaxStatFactions, actor.playerPartnerClimaxStatFactions);
        VectorUtil::mergeDistinct(playerStatLists, actor.playerStatLists);
        VectorUtil::mergeDistinct(playerClimaxStatLists, actor.playerClimaxStatLists);
        VectorUtil::mergeDistinct(playerPartnerClimaxStatLists, actor.playerPartnerClimaxStatLists);
    }


    bool GraphActor::hasTag(std::string tag) {
        StringUtil::toLower(&tag);
        for (GraphActorTag& actorTag : tags) {
            if (actorTag.tag == tag) {
                return true;
            }
        }
        return false;
    }

    bool GraphActor::hasAnyTag(std::vector<std::string> tags) {
        for (std::string& tag : tags) {
            if (hasTag(tag)) {
                return true;
            }
        }
        return false;
    }

    bool GraphActor::hasAllTags(std::vector<std::string> tags) {
        for (std::string& tag : tags) {
            if (!hasTag(tag)) {
                return false;
            }
        }
        return true;
    }

    bool GraphActor::hasOnlyTags(std::vector<std::string> tags) {
        for (GraphActorTag& actorTag : this->tags) {
            if (!VectorUtil::contains(tags, actorTag.tag)) {
                return false;
            }
        }
        return true;
    }
    

    bool GraphActor::hasTag(const char* tag) {
        return hasTag(std::string(tag));
    }

    uint32_t GraphActor::getTagCount() {
        return tags.size();
    }

    OStim::NodeActorTag* GraphActor::getTag(uint32_t index) {
        if (index < 0 || index >= tags.size()) {
            return nullptr;
        }

        return &tags[index];
    }

    void GraphActor::forEachTag(OStim::NodeActorTagVisitor* visitor) {
        for (GraphActorTag& tag : tags) {
            if (!visitor->visit(&tag)) {
                break;
            }
        }
    }
}