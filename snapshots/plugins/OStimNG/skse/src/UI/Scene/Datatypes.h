#pragma once

namespace UI::Scene {
	struct OptionData {
		std::string nodeId;
		std::string title;
		std::string imagePath;
		std::string border;
		std::string description;
		void loadValues(RE::GFxValue& option) {
			option.SetMember("NodeID", RE::GFxValue{ nodeId.c_str() });
			option.SetMember("Title", RE::GFxValue{ title.c_str() });
			option.SetMember("ImagePath", RE::GFxValue{ imagePath.c_str() });
			option.SetMember("Border", RE::GFxValue{ border.c_str() });
			option.SetMember("Description", RE::GFxValue{ description.c_str() });
		}
	};

	struct MenuData {
		std::vector<OptionData> options;
		void loadValues(RE::GPtr<RE::GFxMovieView> view, RE::GFxValue& menu) {
			for (auto& option : options) {
				RE::GFxValue optionValue;
				view->CreateObject(&optionValue);
				option.loadValues(optionValue);
				menu.PushBack(optionValue);
			}
		}
	};

	
}