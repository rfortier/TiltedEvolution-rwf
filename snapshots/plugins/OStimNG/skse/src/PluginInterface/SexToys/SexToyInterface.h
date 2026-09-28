#pragma once

#include "SexToy.h"
#include "SexToyReloadListener.h"

#include "../PluginInterface.h"

namespace OStim{
    class SexToyInterface : public PluginInterface {
    public:
        inline static const char* NAME = "SexToys";

        // --- ABI version 1 ---

        /**
        * register your listener once in the post load event
        */
        virtual void registerReloadListener(SexToyReloadListener* listener) = 0;

        /**
        * toys have to be re-registered every time the reloadToys function of the reload listener is called
        */
        virtual void addToy(SexToy* toy) = 0;
    };
}