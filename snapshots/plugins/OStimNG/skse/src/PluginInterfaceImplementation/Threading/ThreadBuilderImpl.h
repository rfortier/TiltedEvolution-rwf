#pragma once

#include "PluginInterface/Threading/ThreadBuilder.h"
#include "Core/ThreadStarter/ThreadStartParams.h"

namespace Interface {
    class ThreadBuilderImpl : public OStim::ThreadBuilder {
    public:
        ThreadBuilderImpl(std::vector<GameAPI::GameActor> actors);

        virtual void setDominantActors(uint32_t actorCount, void** actors) override;
        virtual void setFurniture(void* furniture) override;
        virtual void setDuration(int32_t duration) override;
        virtual void setStartingNode(OStim::Node* node) override;
        virtual void setStartingNode(const char* node) override;
        virtual void addStartingNode(OStim::Node* node, int32_t duration, bool navigateTo) override;
        virtual void addStartingNode(const char* node, int32_t duration, bool navigateTo) override;
        virtual void setStartingSequence(const char* sequence) override;
        virtual void concatStartingSequence(const char* sequence, bool navigateTo) override;
        virtual void endAfterSequence() override;
        virtual void undressActors() override;
        virtual void noAutoMode() override;
        virtual void noPlayerControl() override;
        virtual void noUndressing() override;
        virtual void noFurniture() override;
        virtual void setMetadataCSV(const char* metadata) override;
        virtual int32_t start() override;
        virtual void cancel() override;

        virtual bool setStartingNodeSafe(OStim::Node* node) override;
        virtual bool setStartingNodeSafe(const char* node) override;
        virtual bool addStartingNodeSafe(OStim::Node* node, int32_t duration, bool navigateTo) override;
        virtual bool addStartingNodeSafe(const char* node, int32_t duration, bool navigateTo) override;

    private:
        Threading::ThreadStartParams params;
    };
}