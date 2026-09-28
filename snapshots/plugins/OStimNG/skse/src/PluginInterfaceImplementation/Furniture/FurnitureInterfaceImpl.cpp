#include "FurnitureInterfaceImpl.h"

#include "../InterfaceMapImpl.h"

#include "Furniture/FurnitureTable.h"

namespace Interface {
    uint32_t FurnitureInterfaceImpl::getVersion() {
        return SKSE::PluginDeclaration::GetSingleton()->GetVersion().pack();
    }


    OStim::FurnitureType* FurnitureInterfaceImpl::getType(const char* id) {
        return Furniture::FurnitureTable::getFurnitureType(std::string(id));
    }

    OStim::FurnitureType* FurnitureInterfaceImpl::getType(void* furniture) {
        return Furniture::FurnitureTable::getFurnitureType(furniture, false);
    }
}