#include "ToyThread.h"

#include "Settings/Settings.h"
#include "ToyTable.h"

namespace Toys {
    ToyThread::ToyThread(Threading::Thread* thread) : thread{thread} {
        thread->registerLoopListener([this]() { loop(); });
        thread->registerPeakListener([this](actionIndex action) { peak(action); });
        thread->registerSpeedChangedListener([this]() { speedChanged(); });
        thread->registerNodeChangedListener([this]() { nodeChanged(); });
        thread->registerThreadEndListener([this]() { threadEnd(); });

        nodeChanged();
    }

    void ToyThread::loop() {
        for (ToyGroup& group : groups) {
            group.loop();
        }
    }

    void ToyThread::peak(actionIndex action) {
        for (ToyGroup& group : groups) {
            if (group.isAction(action)) {
                group.peak();
            }
        }
    }

    void ToyThread::speedChanged() {
        for (ToyGroup& group : groups) {
            group.speedChanged();
        }
    }

    void ToyThread::nodeChanged() {
        for (ToyGroup& group : groups) {
            group.stop();
        }

        groups.clear();

        
        std::vector<ToyWrapper>* toys = ToyTable::getSingleton()->getToys();
        std::set<ToyWrapper*> toysInUse;

        for (Toys::ToyWrapper& toy : *toys) {
            std::string id = toy.getID();
            if (!Settings::Settings::getSingleton()->getToySettings(id)->getSlotSettings(Settings::SlotSettings::GLOBAL_KEY)->enabled) {
                toysInUse.insert(&toy);
            }
        }

        Settings::ToySettings* globalSettings = Settings::Settings::getSingleton()->getToySettings(Settings::ToySettings::GLOBAL_KEY);
        Settings::SlotSettings* globalSlotSettings = globalSettings->getSlotSettings(Settings::SlotSettings::GLOBAL_KEY);

        Threading::Thread* thread = this->thread;
        for (Graph::Action::Action& action : thread->getCurrentNodeInternal()->actions) {
            if (toysInUse.size() == toys->size()) {
                break;
            }

            std::vector<std::tuple<Threading::ThreadActor*, std::string, ToyWrapper*>> slotToys;
            action.roles.forEach([globalSettings, globalSlotSettings, &slotToys, &thread, &action, &toys, &toysInUse](Graph::Role role, int index) {
                Graph::Action::ActionActor* actionActor = action.attributes->roles.get(role);
                for (std::string slot : actionActor->toySlots) {
                    Threading::ThreadActor* actor = thread->GetActor(index);
                    for (ToyWrapper& toy : *toys) {
                        if (toysInUse.contains(&toy)) {
                            continue;
                        }

                        std::string id = toy.getID();
                        Settings::ToySettings* settings = Settings::Settings::getSingleton()->getToySettings(id);
                        Settings::SlotSettings* slotSettings = settings->getSlotSettings(slot);

                        if (!slotSettings->enabled || !globalSettings->getSlotSettings(slot)->enabled) {
                            continue;
                        }

                        Settings::SynchronizationType syncType = getSynchronizationType(slot, globalSettings, globalSlotSettings, settings);

                        bool doToy = false;
                        switch (syncType) {
                            case Settings::SynchronizationType::PLAYER: {
                                if (actor) {
                                    doToy = actor->getActor().isPlayer();
                                }
                            } break;
                            case Settings::SynchronizationType::MALE: {
                                for (auto& [index, threadActor] : thread->getActors()) {
                                    if (threadActor.getActor().isSex(GameAPI::GameSex::MALE)) {
                                        doToy = actor == &threadActor;
                                        break;
                                    }
                                }
                            } break;
                            case Settings::SynchronizationType::FEMALE: {
                                for (auto& [index, threadActor] : thread->getActors()) {
                                    if (threadActor.getActor().isSex(GameAPI::GameSex::FEMALE)) {
                                        doToy = actor == &threadActor;
                                        break;
                                    }
                                }
                            } break;
                            case Settings::SynchronizationType::SCENE:
                            case Settings::SynchronizationType::INDIVIDUAL_SYNCHRONIZATION: {
                                doToy = true;
                            } break;
                        }

                        if (doToy) {
                            toysInUse.insert(&toy);
                            slotToys.push_back({actor, slot, &toy});
                        }
                    }
                }
            });

            if (!slotToys.empty()) {
                groups.push_back({&action, slotToys});
            }
        }
    }

    void ToyThread::threadEnd() {
        for (ToyGroup& group : groups) {
            group.stop();
        }

        delete this;
    }


    Settings::SynchronizationType ToyThread::getSynchronizationType(std::string& slot, Settings::ToySettings* globalSettings, Settings::SlotSettings* globalSlotSettings, Settings::ToySettings* toySettings) {
        Settings::SynchronizationType syncType = globalSlotSettings->synchronizationSettings.synchronizationType;
        if (syncType != Settings::SynchronizationType::INDIVIDUAL_SYNCHRONIZATION) {
            return syncType;
        }

        syncType = globalSettings->getSlotSettings(slot)->synchronizationSettings.synchronizationType;
        if (syncType != Settings::SynchronizationType::INDIVIDUAL_SYNCHRONIZATION) {
            return syncType;
        }

        syncType = toySettings->getSlotSettings(Settings::SlotSettings::GLOBAL_KEY)->synchronizationSettings.synchronizationType;
        if (syncType != Settings::SynchronizationType::INDIVIDUAL_SYNCHRONIZATION) {
            return syncType;
        }

        syncType = toySettings->getSlotSettings(slot)->synchronizationSettings.synchronizationType;
        return syncType;
    }
}