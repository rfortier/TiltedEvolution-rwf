#include "GraphInterfaceImpl.h"

#include "Graph/GraphTable.h"

namespace Interface {
    uint32_t GraphInterfaceImpl::getVersion() {
        return SKSE::PluginDeclaration::GetSingleton()->GetVersion().pack();
    }

    OStim::ActionTag* GraphInterfaceImpl::getActionTag(const char* id) {
        return Graph::GraphTable::getActionTag(id);
    }

    OStim::ActionType* GraphInterfaceImpl::getActionType(const char* id) {
        return Graph::GraphTable::getActionTypeUnsafe(id);
    }
}