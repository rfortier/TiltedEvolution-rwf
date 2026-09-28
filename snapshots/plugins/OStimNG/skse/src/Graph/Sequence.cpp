#include "Sequence.h"

#include "Node.h"

namespace Graph {
    bool Sequence::fulfilledBy(std::vector<Trait::ActorCondition> conditions) {
        for (SequenceEntry entry : nodes) {
            if (!entry.node->fulfilledBy(conditions)) {
                return false;
            }
        }
        return true;
    }
}