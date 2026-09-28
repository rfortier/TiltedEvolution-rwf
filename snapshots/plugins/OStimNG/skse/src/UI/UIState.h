#pragma once

#include "Core/Singleton.h"
#include "UIInterop.h"
#include "Align/AlignMenu.h"
#include "Scene/SceneMenu.h"
#include "Search/SearchMenu.h"

namespace UI {

    inline static void RegisterMenus() {
        UI::Align::AlignMenu::Register();
        UI::Scene::SceneMenu::Register();
        UI::Search::SearchMenu::Register();
    }

    inline static void PostRegisterMenus() {
        UI::Align::AlignMenu::GetMenu()->PostRegister();
        UI::Scene::SceneMenu::GetMenu()->PostRegister();
        UI::Search::SearchMenu::GetMenu()->PostRegister();
    }

    inline static void HideMenus() {
        UI::Align::AlignMenu::GetMenu()->Hide();
        UI::Scene::SceneMenu::GetMenu()->Hide();
        UI::Search::SearchMenu::GetMenu()->Hide();
    }

    enum MenuType { kSceneMenu, kAlignMenu, kSearchMenu };
    class UIState : public Threading::ISingleton<UIState> {
    public:
        void HandleControl(Controls control);
        void CloseActiveMenu();
        void SwitchActiveMenu(MenuType type);
        void ToggleActiveMenu(MenuType type, bool force = false);
        inline MenuType GetActiveMenu() { return activeMenu; }
        inline void setGlpyh(int glyph) { this->glyph = glyph; }
        void loop();
        void SetThread(Threading::Thread* thread);
        void NodeChanged(Threading::Thread* thread, Graph::Node* node);
        void SpeedChanged(Threading::Thread* thread, int speed);
        void HandleThreadRemoved(Threading::Thread* thread);

        // External UI support (e.g., OStim Prism)
        static void SetExternalUIEnabled(bool enabled) { useExternalUI = enabled; }
        static bool IsExternalUIEnabled() { return useExternalUI; }


        Threading::Thread* currentThread;
        Graph::Node* currentNode;
    private:
        // TODO remove this again when we no longer need OSA
        int glyph = 0;
        MenuType activeMenu = MenuType::kSceneMenu;
        inline static bool useExternalUI = false;
        const short UI_UPDATE_LOOP_TIME = 10000;
        short refreshUIPositionCooldown = UI_UPDATE_LOOP_TIME;
    };

    class doHideMenuRequest : public RE::GFxFunctionHandler {
    public:
        void Call(Params& args) override {
            auto uiState = UI::UIState::GetSingleton();
            uiState->CloseActiveMenu();
        }
    };

    class doShowAlignMenu : public RE::GFxFunctionHandler {
    public:
        void Call(Params& args) override {
            UI::UIState::GetSingleton()->ToggleActiveMenu(UI::kAlignMenu);
        }
    };

    class doShowSearchMenu : public RE::GFxFunctionHandler {
    public:
        void Call(Params& args) override {
            UI::UIState::GetSingleton()->ToggleActiveMenu(UI::kSearchMenu);
        }
    };

    inline static void EndControlledScene() {
        UI::HideMenus();
        UI::UIState::GetSingleton()->CloseActiveMenu();
    }
}  // namespace UI