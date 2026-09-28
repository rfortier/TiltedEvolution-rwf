#include "ThreadStartParams.h"

#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"

namespace Threading {
    void ThreadStartParams::setStartingNode(std::string nodeID) {
        Graph::Node* node = Graph::GraphTable::getNodeById(nodeID);
        if (!node) {
            logger::warn("animation {} could not be found", nodeID);
            return;
        }

        startingNodes = {{node->animationLengthMs, node}};
    }

    void ThreadStartParams::addStartingNode(std::string nodeID, int duration, bool navigateTo) {
        Graph::Node* node = Graph::GraphTable::getNodeById(nodeID);
        if (!node) {
            logger::warn("animation {} could not be found", nodeID);
            return;
        }

        if (navigateTo && !startingNodes.empty() && startingNodes.back().node != node) {
            std::vector<Graph::SequenceEntry> path = startingNodes.back().node->getRoute(MCM::MCMTable::navigationDistanceMax(), Trait::ActorCondition::create(actors), node);
            if (path.empty()) {
                startingNodes.push_back({duration > 0 ? duration : node->animationLengthMs, node});
            } else {
                for (Graph::SequenceEntry& entry : path) {
                    startingNodes.push_back(entry);
                }

                if (duration > 0.0f) {
                    startingNodes.back().duration = duration;
                }
            }

        } else {
            startingNodes.push_back({duration > 0 ? duration : node->animationLengthMs, node});
        }
    }

    void ThreadStartParams::setStartingSequence(std::string sequenceID) {
        Graph::Sequence* sequence = Graph::GraphTable::getSequenceByID(sequenceID);
        if (!sequence) {
            logger::warn("sequence {} could not be found", sequenceID);
            return;
        }

        startingNodes.clear();
        for (Graph::SequenceEntry& entry : sequence->nodes) {
            startingNodes.push_back(entry);
        }
    }

    void ThreadStartParams::concatStartingSequence(std::string sequenceID, bool navigateTo) {
        Graph::Sequence* sequence = Graph::GraphTable::getSequenceByID(sequenceID);
        if (!sequence) {
            logger::warn("sequence {} could not be found", sequenceID);
            return;
        }

        if (navigateTo && !startingNodes.empty() && startingNodes.back().node != sequence->nodes.front().node) {
            std::vector<Graph::SequenceEntry> path = startingNodes.back().node->getRoute(MCM::MCMTable::navigationDistanceMax(), Trait::ActorCondition::create(actors), sequence->nodes.front().node);
            if (!path.empty()) {
                for (Graph::SequenceEntry& entry : path) {
                    startingNodes.push_back(entry);
                }
                startingNodes.pop_back();
            }
        }

        for (Graph::SequenceEntry& entry : sequence->nodes) {
            startingNodes.push_back(entry);
        }
    }
}