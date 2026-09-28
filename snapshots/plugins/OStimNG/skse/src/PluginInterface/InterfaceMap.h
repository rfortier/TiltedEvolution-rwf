#pragma once

#include "PluginInterface.h"

namespace OStim {
    class InterfaceMap {
    public:
        virtual PluginInterface* queryInterface(const char* name) = 0;
        virtual bool addInterface(const char* name, PluginInterface* pluginInterface) = 0;
        virtual PluginInterface* removeInterface(const char* name) = 0;
    };
}