#pragma once

#include "PluginInterface/SexToys/SexToyInterface.h"
#include "Util/Singleton.h"

namespace Interface {
    class SexToyInterfaceImpl : public OStim::SexToyInterface, public Util::Singleton<SexToyInterfaceImpl> {
    public:
        virtual uint32_t getVersion() override;
        virtual void registerReloadListener(OStim::SexToyReloadListener* listener) override;
        virtual void addToy(OStim::SexToy* toy) override;
    };
}