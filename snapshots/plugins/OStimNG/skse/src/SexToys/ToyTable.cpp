#include "ToyTable.h"

#include "Settings/Settings.h"
#include "ToyThread.h"

#include "Core/ThreadManager.h"

namespace Toys {
    ToyTable::ToyTable() {
        Threading::ThreadManager::GetSingleton()->registerThreadStartListener([this](Threading::Thread* thread) {
            if (thread->isPlayerThread()) {
                reloadToys();
            }
        });

        reloadToys();
    }


    void ToyTable::addToySlots(std::vector<std::string>& slots) {
        for (std::string slot : slots) {
            toySlots.insert(slot);
        }
    }


    void ToyTable::reloadToys() {
        std::unique_lock reloadLock(_reloadLock);
        if (isReloading) {
            return;
        }

        isReloading = true;
        toys.clear();
        std::thread([this]() {
            for (OStim::SexToyReloadListener* listener : reloadListeners) {
                listener->reloadToys();
            }
            isReloading = false;

            if (!Settings::Settings::getSingleton()->getToySettings(Settings::ToySettings::GLOBAL_KEY)->getSlotSettings(Settings::SlotSettings::GLOBAL_KEY)->enabled) {
                return;
            }

            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->getPlayerThread();
            if (thread) {
                new ToyThread(thread);
            }
        }).detach();
    }

    void ToyTable::addToy(OStim::SexToy* toy) {
        toys.push_back({toy});
        logger::info("registered toy '{}'", toy->getName());
    }

    ToyWrapper* ToyTable::getToy(std::string id) {
        if (isReloading) {
            return nullptr;
        }

        for (ToyWrapper& toy : toys) {
            if (std::string(toy.getID()) == id) {
                return &toy;
            }
        }

        return nullptr;
    }
}