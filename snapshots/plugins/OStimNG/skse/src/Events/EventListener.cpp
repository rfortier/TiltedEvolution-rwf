#include "EventListener.h"

#include "Core/ThreadStarter/DebugNPCThreadStarter.h"
#include "Core/ThreadStarter/ThreadBuilder.h"
#include "Core/ThreadStarter/ThreadStarter.h"
#include "Core/Core.h"
#include "Core/ThreadManager.h"
#include "GameAPI/Game.h"
#include "GameAPI/GameCamera.h"
#include "GameLogic/GameTable.h"
#include "MCM/MCMTable.h"
#include "Serial/Manager.h"
#include "SexToys/ToyTable.h"
#include "UI/Align/AlignMenu.h"
#include "UI/UIState.h"
#include "Util/Globals.h"
#include "Util/MathUtil.h"
#include "Util.h"

namespace Events {
    RE::BSEventNotifyControl EventListener::ProcessEvent(const SKSE::NiNodeUpdateEvent* a_event, RE::BSTEventSource<SKSE::NiNodeUpdateEvent>* a_eventSource) {
        if (a_event->reference->Is(RE::Actor::FORMTYPE)) {
            RE::Actor* actor = a_event->reference->As<RE::Actor>();
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                threadActor->handleNiNodeUpdate();
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl EventListener::ProcessEvent(const SKSE::CrosshairRefEvent* a_event, RE::BSTEventSource<SKSE::CrosshairRefEvent>* a_eventSource) {
        if (a_event->crosshairRef) {
            // weird As call has to be made to make it non const
            GameLogic::GameTable::setCurrentCrosshairRef(a_event->crosshairRef->As<RE::TESObjectREFR>());
        } else {
            GameLogic::GameTable::setCurrentCrosshairRef(nullptr);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl EventListener::ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) {
        if (!a_events) {
            return RE::BSEventNotifyControl::kContinue;
        }
        
        if (RE::UI::GetSingleton()->GameIsPaused() && UI::UIState::GetSingleton()->GetActiveMenu() != UI::MenuType::kSearchMenu) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (!RE::UI::GetSingleton()->IsCursorHiddenWhenTopmost()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (RE::InputEvent* iEvent = *a_events; iEvent; iEvent = iEvent->next) {
            if (iEvent->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
                continue;
            }

            RE::ButtonEvent* bEvent = iEvent->AsButtonEvent();

            uint32_t keyCode;
            RE::INPUT_DEVICE deviceType = bEvent->GetDevice();
            uint32_t keyMask = bEvent->GetIDCode();

            if (deviceType == RE::INPUT_DEVICE::kMouse) {
                keyCode = 256 + keyMask;
            } else if (deviceType == RE::INPUT_DEVICE::kGamepad) {
                keyCode = util::gamepadMaskToKeyCode(keyMask);
            } else {
                keyCode = keyMask;
            }

            if (keyCode == 0 || keyCode == 1 || keyCode >= 282) {
                // invalid key
                continue;
            }

            if (keyCode == MCM::MCMTable::keySceneStart()) {
                if (Threading::ThreadManager::GetSingleton()->playerThreadRunning()) {
                    if (bEvent->IsDown()) {
                        Threading::Thread* playerThread = Threading::ThreadManager::GetSingleton()->getPlayerThread();
                        Furniture::selectFurniture(playerThread->getActorCount(), playerThread->getCenter(), MCM::MCMTable::furnitureSearchDistance(), 96.0f, true, [playerThread](GameAPI::GameObject furniture) {
                            playerThread->changeFurniture(furniture, nullptr);
                        });
                    }
                } else {
                    if (bEvent->IsDown()) {
                        GameAPI::GameActor player = GameAPI::GameActor::getPlayer();
                        GameAPI::GameActor target = GameAPI::Game::getCrosshairActor();
                        if (target) {
                            Threading::ThreadStartParams params = {.actors = {player, target}};
                            Threading::startThread(params);
                        }
                    } else if (bEvent->IsUp() && bEvent->HeldDuration() >= 2.0) {
                        Threading::ThreadStartParams params = {.actors = {GameAPI::GameActor::getPlayer()}};
                        Threading::startThread(params);
                    }
                }
                continue;
            } else if (keyCode == MCM::MCMTable::keyNpcSceneStart()) {
                if (bEvent->IsDown()) {
                    if (Threading::ThreadManager::GetSingleton()->playerThreadRunning()) {
                        continue;
                    }

                    GameAPI::GameActor target = GameAPI::Game::getCrosshairActor();
                    if (!target) {
                        continue;
                    }

                    Threading::ThreadStartParams params = {.actors = {target}};
                    Threading::startDebugNPCThread(params);
                }
                continue;
            }

            if (!bEvent->IsDown()) {
                continue;
            }

            if (!Threading::ThreadManager::GetSingleton()->playerThreadRunning()) {
                continue;
            }

            // TODO do this internally once we don't need OSA anymore
            const auto skyrimVM = RE::SkyrimVM::GetSingleton();
            auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
            if (vm) {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                auto args = RE::MakeFunctionArguments();
                vm->DispatchStaticCall("OSKSE", "ShowBars", args, callback);
            }

            if (keyCode == MCM::MCMTable::keyUp()) {
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::Up);
            } else if (keyCode == MCM::MCMTable::keyDown()) {
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::Down);
            } else if (keyCode == MCM::MCMTable::keyLeft()) {
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::Left);
            } else if (keyCode == MCM::MCMTable::keyRight()) {
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::Right);
            } else if (keyCode == MCM::MCMTable::keyYes()) {
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::Yes);
            } else if (keyCode == MCM::MCMTable::keyToggle()) {
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::Toggle);
            } else if (keyCode == MCM::MCMTable::keyEnd()) {
                Threading::ThreadManager::GetSingleton()->getPlayerThread()->stopFaded();
            } else if (keyCode == MCM::MCMTable::keySpeedUp()) {
                if (Threading::ThreadManager::GetSingleton()->getPlayerThread()->increaseSpeed()) {
                    UI::Scene::SceneMenu::GetMenu()->SpeedUp();
                }
            } else if (keyCode == MCM::MCMTable::keySpeedDown()) {
                if (Threading::ThreadManager::GetSingleton()->getPlayerThread()->decreaseSpeed()) {
                    UI::Scene::SceneMenu::GetMenu()->SpeedDown();
                }
            } else if (keyCode == MCM::MCMTable::keyPullOut()){
                if (UI::UIState::GetSingleton()->GetActiveMenu() == UI::MenuType::kSceneMenu) {
                    Threading::ThreadManager::GetSingleton()->getPlayerThread()->pullOut();
                } else {
                    UI::UIState::GetSingleton()->HandleControl(UI::Controls::No);
                }
            } else if (keyCode == MCM::MCMTable::keyAutoMode()){
                Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->getPlayerThread();
                if (thread) {
                    if (thread->isInAutoMode()) {
                        thread->stopAutoMode();
                    } else {
                        thread->startAutoMode();
                    }
                }
            } else if (keyCode == MCM::MCMTable::keyFreeCam()) {
                GameAPI::GameCamera::toggleFreeCam();
            } else if (keyCode == MCM::MCMTable::keyAlignment()) {
                UI::UIState::GetSingleton()->ToggleActiveMenu(UI::kAlignMenu);
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::AlignMenu);
            } else if (keyCode == MCM::MCMTable::keySearch()) {
                UI::UIState::GetSingleton()->ToggleActiveMenu(UI::kSearchMenu);
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::SearchMenu);
            } else if (keyCode == MCM::MCMTable::keyHideUI()) {
                UI::UIState::GetSingleton()->HandleControl(UI::Controls::KEY_HIDE);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    
    void EventListener::handleGameLoad() {
        const auto skyrimVM = RE::SkyrimVM::GetSingleton();
        auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
        if (vm) {
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new CheckPapyrusUndressingCallbackFunctor());
            auto args = RE::MakeFunctionArguments();
            vm->DispatchStaticCall("OUndress", "UsePapyrusUndressing", args, callback);
        }
    }
}