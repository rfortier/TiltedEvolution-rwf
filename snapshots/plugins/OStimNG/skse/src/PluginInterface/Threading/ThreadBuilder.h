#pragma once

#include "Thread.h"

#include "../Graph/Node.h"

namespace OStim {
    class ThreadBuilder {
    public:
        // --- ABI version 2 ---
        virtual void setDominantActors(uint32_t actorCount, void** actors) = 0;
        virtual void setFurniture(void* furniture) = 0;
        virtual void setDuration(int32_t duration) = 0;
        virtual void setStartingNode(Node* node) = 0;
        virtual void setStartingNode(const char* node) = 0;
        virtual void addStartingNode(Node* node, int32_t duration, bool navigateTo) = 0;
        virtual void addStartingNode(const char* node, int32_t duration, bool navigateTo) = 0;
        virtual void setStartingSequence(const char* sequence) = 0;
        virtual void concatStartingSequence(const char* sequence, bool navigateTo) = 0;
        virtual void endAfterSequence() = 0;
        virtual void undressActors() = 0;
        virtual void noAutoMode() = 0;
        virtual void noPlayerControl() = 0;
        virtual void noUndressing() = 0;
        virtual void noFurniture() = 0;
        virtual void setMetadataCSV(const char* metadata) = 0;
        virtual int32_t start() = 0;
        virtual void cancel() = 0;

        // --- ABI version 0x07050010 ---
        virtual bool setStartingNodeSafe(Node* node) = 0;
        virtual bool setStartingNodeSafe(const char* node) = 0;
        virtual bool addStartingNodeSafe(Node* node, int32_t duration, bool navigateTo) = 0;
        virtual bool addStartingNodeSafe(const char* node, int32_t duration, bool navigateTo) = 0;
    };
}