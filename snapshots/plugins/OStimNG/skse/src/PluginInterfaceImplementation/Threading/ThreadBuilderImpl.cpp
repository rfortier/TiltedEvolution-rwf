#include "ThreadBuilderImpl.h"

#include "Core/ThreadStarter/ThreadStarter.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"
#include "Util/StringUtil.h"

namespace Interface {
    ThreadBuilderImpl::ThreadBuilderImpl(std::vector<GameAPI::GameActor> actors) {
        params.actors = actors;
    }


    void ThreadBuilderImpl::setDominantActors(uint32_t actorCount, void** actors) {
        for (int i = 0; i < actorCount; i++) {
            params.dominantActors.push_back(*(actors + i));
        }
    }

    void ThreadBuilderImpl::setFurniture(void* furniture) {
        params.furniture = furniture;
    }

    void ThreadBuilderImpl::setDuration(int32_t duration) {
        params.duration = duration;
    }

    void ThreadBuilderImpl::setStartingNode(OStim::Node* node) {
        params.setStartingNode(node->getNodeID());
    }

    void ThreadBuilderImpl::setStartingNode(const char* nodeID) {
        params.setStartingNode(nodeID);
    }

    void ThreadBuilderImpl::addStartingNode(OStim::Node* node, int32_t duration, bool navigateTo) {
        params.addStartingNode(node->getNodeID(), duration, navigateTo);   
    }

    void ThreadBuilderImpl::addStartingNode(const char* nodeID, int32_t duration, bool navigateTo) {
        params.addStartingNode(nodeID, duration, navigateTo);   
    }

    void ThreadBuilderImpl::setStartingSequence(const char* sequence) {
        params.setStartingSequence(sequence);
    }

    void ThreadBuilderImpl::concatStartingSequence(const char* sequence, bool navigateTo) {
        params.concatStartingSequence(sequence, navigateTo);
    }

    void ThreadBuilderImpl::endAfterSequence() {
        params.endAfterSequence = true;
    }

    void ThreadBuilderImpl::undressActors() {
        params.threadFlags |= Threading::ThreadFlag::UNDRESS;
    }

    void ThreadBuilderImpl::noAutoMode() {
        params.threadFlags |= Threading::ThreadFlag::NO_AUTO_MODE;
    }

    void ThreadBuilderImpl::noPlayerControl() {
        params.threadFlags |= Threading::ThreadFlag::NO_PLAYER_CONTROL;
    }

    void ThreadBuilderImpl::noUndressing() {
        params.threadFlags |= Threading::ThreadFlag::NO_UNDRESSING;
    }

    void ThreadBuilderImpl::noFurniture() {
        params.noFurniture = true;
    }

    void ThreadBuilderImpl::setMetadataCSV(const char* metadata) {
        std::string stddata = std::string(metadata);
        StringUtil::toLower(&stddata);
        std::vector<std::string> metadataVector = StringUtil::toTagVector(metadata);
        params.metadata = metadataVector;
    }

    int32_t ThreadBuilderImpl::start() {
        int32_t id = Threading::startThread(params);
        delete this;
        return id;
    }

    void ThreadBuilderImpl::cancel() {
        delete this;
    }


    bool ThreadBuilderImpl::setStartingNodeSafe(OStim::Node* node) {
        Graph::Node* internalNode = Graph::GraphTable::getNodeById(node->getNodeID());
        std::vector<Trait::ActorCondition> conditions = Trait::ActorCondition::create(params.actors);
        if (!internalNode->fulfilledBy(conditions)) {
            return false;
        }

        params.setStartingNode(node->getNodeID()); // TODO if I already have the internal node I might aswell just use it
        return true;
    }

    bool ThreadBuilderImpl::setStartingNodeSafe(const char* nodeID) {
        Graph::Node* node = Graph::GraphTable::getNodeById(nodeID);
        std::vector<Trait::ActorCondition> conditions = Trait::ActorCondition::create(params.actors);
        if (!node->fulfilledBy(conditions)) {
            return false;
        }

        params.setStartingNode(nodeID);  // TODO if I already have the node I might aswell just use it
        return true;
    }

    bool ThreadBuilderImpl::addStartingNodeSafe(OStim::Node* node, int32_t duration, bool navigateTo) {
        Graph::Node* internalNode = Graph::GraphTable::getNodeById(node->getNodeID());
        std::vector<Trait::ActorCondition> conditions = Trait::ActorCondition::create(params.actors);
        if (!internalNode->fulfilledBy(conditions)) {
            return false;
        }

        params.addStartingNode(node->getNodeID(), duration, navigateTo); // TODO if I already have the internal node I might aswell just use it
        return true;
    }

    bool ThreadBuilderImpl::addStartingNodeSafe(const char* nodeID, int32_t duration, bool navigateTo) {
        Graph::Node* node = Graph::GraphTable::getNodeById(nodeID);
        std::vector<Trait::ActorCondition> conditions = Trait::ActorCondition::create(params.actors);
        if (!node->fulfilledBy(conditions)) {
            return false;
        }

        params.addStartingNode(nodeID, duration, navigateTo);  // TODO if I already have the node I might aswell just use it
        return true;
    }
}