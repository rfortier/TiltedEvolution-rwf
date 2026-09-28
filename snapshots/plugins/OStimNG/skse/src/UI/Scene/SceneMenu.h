#pragma once
#include <UI/UIInterop.h>
#include <UI/Settings.h>
#include <UI/OStimMenu.h>
#include "SceneOptions.h"
#include <Core/Thread.h>
#include <Core/Singleton.h>
#include "Datatypes.h"

namespace UI::Scene {
	class SceneMenu : public UI::OStimMenu {
	public:
		using Super = UI::OStimMenu;

	public:
		static constexpr const char* MENU_NAME = "OstimSceneMenu";
		static constexpr std::int8_t SORT_PRIORITY{ 3 };

		SceneMenu();
		inline static SceneMenu* GetMenu() {
			return static_cast<SceneMenu*>(RE::UI::GetSingleton()->GetMenu(MENU_NAME).get());
		}
		static void Register() { OStimMenu::Register(MENU_NAME, Creator); }
		static RE::stl::owner<RE::IMenu*> Creator() { return new SceneMenu(); }
		void PostRegister() override;

		void Show();		
		void Hide();
		
		void Handle(UI::Controls control) override;
		void ApplyPositions();

		void ChangeAnimation(std::string nodeId);
		void ChangeSpeed(bool up);
		void UpdateMenuData();
		void HideSpeed();
		void UpdateSpeed();

		void SpeedUp();
		void SpeedDown();
		
		bool IsOptionsOpen() { return optionsOpen; }	
		void HandleOption(std::string idx);
		void SetOptionsOpen(bool isOpen) {
			optionsOpen = isOpen;
		}
		void BuildOptionsData();

	private:
		void BuildMenuData(MenuData& menudata);
		void SendControl(int32_t control);
		void GetOptionBoxes(RE::GFxValue& optionBoxes);
		void GetMenuSelectorMenu(RE::GFxValue& settingsMenu);

	private:
		bool optionsOpen = false;
	};

	class doSelectOption : public RE::GFxFunctionHandler {
	public:
		void Call(Params& args) override {
			auto sceneMenu = UI::Scene::SceneMenu::GetMenu();
			if (sceneMenu->IsOptionsOpen()) {
				sceneMenu->HandleOption(args.args[0].GetString());
			}
			else {
				sceneMenu->ChangeAnimation(args.args[0].GetString());
			}
		}
	};

	class doChangeSpeed : public RE::GFxFunctionHandler {
		public :
			void Call(Params& args) override {
				logger::info("change speed");
				UI::Scene::SceneMenu::GetMenu()->ChangeSpeed(args.args[0].GetBool());
			}
	};

	class doShowOptions : public RE::GFxFunctionHandler {
	public:
		void Call(Params& args) override {
			logger::info("show options");
			auto sceneMenu = UI::Scene::SceneMenu::GetMenu();
			sceneMenu->SetOptionsOpen(true);			
			sceneMenu->UpdateMenuData();
		}
	};
}