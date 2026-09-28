#include "UI/OStimMenu.h"

namespace UI {
    
    OStimMenu::OStimMenu(std::string menuName) : RE::IMenu() {
        this->menuName = menuName;

        auto scaleformManager = RE::BSScaleformManager::GetSingleton();
        scaleformManager->LoadMovieEx(this, menuName, [](RE::GFxMovieDef* a_def) -> void {
            a_def->SetState(RE::GFxState::StateType::kLog, RE::make_gptr<Logger>().get());
        });

        inputContext = Context::kNone;

        auto menu = static_cast<RE::IMenu*>(this);
        menu->depthPriority = 0;

        _view = menu->uiMovie;

        menuFlags.set(
            RE::UI_MENU_FLAGS::kAlwaysOpen,
            RE::UI_MENU_FLAGS::kRequiresUpdate,
            RE::UI_MENU_FLAGS::kAllowSaving);

        if (uiMovie) {
            uiMovie->SetMouseCursorCount(0);  // disable input            
        }
    }

    void OStimMenu::PostRegister() {}

    void OStimMenu::Register(std::string menuName, RE::UI::Create_t* createFn) {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->Register(menuName, createFn);
            logger::info("Registered {}", menuName);
            
            auto msgQ = RE::UIMessageQueue::GetSingleton();
            if (msgQ) {
                msgQ->AddMessage(menuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
            }
        }
    }

    void OStimMenu::Show() {
        if (_isOpen) {
            return;
        }

        auto msgQ = RE::UIMessageQueue::GetSingleton();
        if (msgQ) {
            msgQ->AddMessage(menuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
        }
        _isOpen = true;
    }

    void OStimMenu::Hide() {
        if (!_isOpen) {
            return;
        }

        auto msgQ = RE::UIMessageQueue::GetSingleton();
        if (msgQ) {
            msgQ->AddMessage(menuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
        _isOpen = false;
    }

    void OStimMenu::AdvanceMovie(float a_interval, std::uint32_t a_currentTime) {
        Update();
        if (_isOpen) {
            RE::IMenu::AdvanceMovie(a_interval, a_currentTime);
        }
    }

    void OStimMenu::GetRoot(RE::GFxValue& root) {
        assert(uiMovie);
        uiMovie->GetVariable(&root, "_root");
    }
}