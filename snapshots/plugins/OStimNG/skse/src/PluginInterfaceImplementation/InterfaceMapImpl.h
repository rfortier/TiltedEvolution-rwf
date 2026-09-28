#pragma once

#include "PluginInterface/InterfaceMap.h"
#include "Util/Singleton.h"

namespace Interface {
    class InterfaceMapImpl : public OStim::InterfaceMap, public Util::Singleton<InterfaceMapImpl> {
    public:
        virtual OStim::PluginInterface* queryInterface(const char* name) override;
        virtual bool addInterface(const char* name, OStim::PluginInterface* pluginInterface) override;
        virtual OStim::PluginInterface* removeInterface(const char* name) override;

        void setupInterfaces();

    private:
        std::unordered_map<const char*, OStim::PluginInterface*> interfaces;
    };
}