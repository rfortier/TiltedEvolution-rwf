#pragma once

#include "PluginInterface/Graph/NodeActorTag.h"

namespace Graph {
    struct GraphActorTag : public OStim::NodeActorTag {
        std::string tag;

        virtual const char* getTagID() override;
    };
}