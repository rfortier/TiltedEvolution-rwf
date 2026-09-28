#pragma once

#include "PluginInterface/Graph/GraphInterface.h"
#include "Util/Singleton.h"

namespace Interface {
    class GraphInterfaceImpl : public OStim::GraphInterface, public Util::Singleton<GraphInterfaceImpl> {
    public:
        virtual uint32_t getVersion() override;

        virtual OStim::ActionTag* getActionTag(const char* id) override;
        virtual OStim::ActionType* getActionType(const char* id) override;
    };
}