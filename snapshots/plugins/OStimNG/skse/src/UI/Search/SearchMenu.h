#pragma once
#include "UI/UIInterop.h"
#include "UI/OStimMenu.h"

namespace UI::Search {
    class SearchMenu : public UI::OStimMenu {        
        struct SearchItem {
            std::string id;
            std::string label;
        };

    public:
        using Super = UI::OStimMenu;

    public:
        static constexpr const char* MENU_NAME = "OStimSearchMenu";
        static constexpr std::int8_t SORT_PRIORITY{ 3 };

        SearchMenu();
        inline static SearchMenu* GetMenu() {
            return static_cast<SearchMenu*>(RE::UI::GetSingleton()->GetMenu(MENU_NAME).get());
        }
        static void Register() { OStimMenu::Register(MENU_NAME, Creator); }
        static RE::stl::owner<RE::IMenu*> Creator() { return new SearchMenu(); }
        void Handle(UI::Controls control) override;
        void Show();
        void Hide();
        void Search(std::string value);
        void SelectOption(std::string val);
        void PostRegister() override;

        void ApplyPositions();
        bool IsInputtingText() { return _inputtingText; }
    private:
        void GetControlHandler(RE::GFxValue& controlHandler);
        void AssignData(std::vector<SearchItem>& data);
        void SendControl(int32_t control);
        void SetInputtingText(bool inputting) { _inputtingText = inputting; }
    private:
        bool _inputtingText = false;

        class doSearchFunction : public RE::GFxFunctionHandler {
        public:
            void Call(Params& args) override {
                UI::Search::SearchMenu::GetMenu()->Search(args.args[0].GetString());
            }
        };

        class doSelectOptionFunction : public RE::GFxFunctionHandler {
        public:
            void Call(Params& args) override {
                UI::Search::SearchMenu::GetMenu()->SelectOption(args.args[0].GetString());
            }
        };

        class doSetInputtingTextFunction : public RE::GFxFunctionHandler {
        public:
            void Call(Params& args) override {
                UI::Search::SearchMenu::GetMenu()->SetInputtingText(args.args[0].GetBool());
            }
        };
    };

    
}