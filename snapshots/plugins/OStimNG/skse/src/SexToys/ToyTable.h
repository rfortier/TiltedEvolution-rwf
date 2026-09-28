#pragma once

#include "ToyWrapper.h"

#include "PluginInterface/SexToys/SexToyReloadListener.h"
#include "Util/Singleton.h"

namespace Toys {
    class ToyTable : public Util::Singleton<ToyTable> {
    public:
        ToyTable();

        inline void addReloadListener(OStim::SexToyReloadListener* listener) { reloadListeners.push_back(listener); }

        void addToySlots(std::vector<std::string>& slots);
        inline std::set<std::string> getToySlots() { return toySlots; }

        void reloadToys();
        void addToy(OStim::SexToy* toy);
        inline std::vector<ToyWrapper>* getToys() { return &toys; }
        ToyWrapper* getToy(std::string id);

    private:
        std::mutex _reloadLock;

        bool isReloading = false;
        std::vector<OStim::SexToyReloadListener*> reloadListeners;

        std::set<std::string> toySlots;
        std::vector<ToyWrapper> toys;
    };
}