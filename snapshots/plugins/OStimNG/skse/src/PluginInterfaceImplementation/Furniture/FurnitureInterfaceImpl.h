#pragma once

#include "PluginInterface/Furniture/FurnitureInterface.h"
#include "Util/Singleton.h"

namespace Interface {
    class FurnitureInterfaceImpl : public OStim::FurnitureInterface, public Util::Singleton<FurnitureInterfaceImpl> {
    public:
        virtual uint32_t getVersion() override;

        virtual OStim::FurnitureType* getType(const char* id) override;
        virtual OStim::FurnitureType* getType(void* furniture) override;
    };
}