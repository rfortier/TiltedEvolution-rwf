#include "UI/UIState.h"
#include "UI/Align/AlignMenu.h"
#include "UI/Scene/SceneMenu.h"
#include "UI/Search/SearchMenu.h"
#include "Core/ThreadManager.h"
#include "ModAPI/OstimNG-API-Thread.h"

namespace UI {
    void UIState::HandleControl(Controls control) {
        if (!Threading::ThreadManager::GetSingleton()->AnySceneRunning()) {
            return;
        }

        // Notify external mods about control input via Thread API
        auto thread = currentThread;
        if (thread) {
            OstimNG_API::Thread::NotifyControlInput(control, thread->m_threadId);
        }

        // When an external UI is active, don't route controls into OStim's Flash menus
        if (useExternalUI) {
            return;
        }

        // Handle control in active menu
        switch (activeMenu) {
        case MenuType::kSceneMenu: {
            UI::Scene::SceneMenu::GetMenu()->Handle(control);
        } break;
        case MenuType::kAlignMenu: {
            UI::Align::AlignMenu::GetMenu()->Handle(control);
        } break;
        case MenuType::kSearchMenu: {
            UI::Search::SearchMenu::GetMenu()->Handle(control);
        }break;
        }
    }

    void UIState::SwitchActiveMenu(MenuType menu) {
        activeMenu = menu;

        // Don't touch the Flash menus when an external UI has taken over
        if (useExternalUI) {
            return;
        }

        UI::Align::AlignMenu::GetMenu()->Hide();
        UI::Search::SearchMenu::GetMenu()->Hide();

        if (menu == MenuType::kAlignMenu) {
            UI::Align::AlignMenu::GetMenu()->Show();
        } else if(menu == MenuType::kSearchMenu) {
            UI::Search::SearchMenu::GetMenu()->Show();
        }
    }
    void UIState::CloseActiveMenu() {
        if (activeMenu != kSceneMenu) {
            ToggleActiveMenu(activeMenu, true);
        }
    }

    void UIState::ToggleActiveMenu(MenuType menu, bool force) {
        if (menu == kSceneMenu)
            return;
        if (activeMenu == kSceneMenu) {
            SwitchActiveMenu(menu);
            return;
        }
        if (activeMenu == menu) {
            if (force || activeMenu != kSearchMenu || !UI::Search::SearchMenu::GetMenu()->IsInputtingText()) {                
                SwitchActiveMenu(kSceneMenu);
            }            
            return;
        }
    }

    void UIState::SetThread(Threading::Thread* thread) {
        currentThread = thread;
        currentNode = thread->getCurrentNodeInternal();
        UI::Align::AlignMenu::GetMenu()->ThreadChanged();
        UI::Scene::SceneMenu::GetMenu()->BuildOptionsData();
        UI::Scene::SceneMenu::GetMenu()->UpdateMenuData();
    }

    void UIState::NodeChanged(Threading::Thread* thread, Graph::Node* node) {
        if (!thread || !node) return;
        if (currentThread != thread) return;
        
        currentNode = node;
        SKSE::GetTaskInterface()->AddTask([thread]() {
            if (UIState::GetSingleton()->currentThread != thread) return;
            
            UI::Align::AlignMenu::GetMenu()->NodeChanged();
            UI::Scene::SceneMenu::GetMenu()->UpdateMenuData();
            UI::Scene::SceneMenu::GetMenu()->UpdateSpeed();
        });        
    }

    void UIState::SpeedChanged(Threading::Thread* thread, int speed) {
        if (!thread) return;
        if (currentThread != thread) return;

        UI::Scene::SceneMenu::GetMenu()->UpdateSpeed();
    }

    void UIState::HandleThreadRemoved(Threading::Thread* thread) {
        if (currentThread == thread) {
            currentThread = nullptr;
        }
    }

    void UIState::loop() {
        /*refreshUIPositionCooldown -= Constants::LOOP_TIME_MILLISECONDS;
        if (refreshUIPositionCooldown <= 0) {
            UI::Settings::LoadSettings();
            UI::Align::AlignMenu::ApplyPositions();
            UI::Scene::SceneMenu::ApplyPositions();
            refreshUIPositionCooldown = UI_UPDATE_LOOP_TIME;
        }*/
    }
    
}  // namespace UI
