#pragma once

#include "PluginInterface/Graph/NodeTag.h"

namespace Graph {
    struct NodeTag : public OStim::NodeTag {
        std::string tag;

        virtual const char* getTagID() override;
    };
}