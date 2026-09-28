#pragma once
#include "Datatypes.h"

#include "Core/Singleton.h"
#include <Graph/Option.h>
#include <Graph/GraphTable.h>
#include <Util/StringUtil.h>


struct MenuPage;
struct MenuOption {
	Graph::OptionNode* node;
	std::string currentState = "";
	GameAPI::GameActor* actor = nullptr;
	std::string equipObjectVariant;
	MenuPage* parent = nullptr;
	std::string getDescription() {
		std::string description = getState().description;
		if (actor != nullptr) {
			StringUtil::replaceAll(description, "{actor}", actor->getName());
		}
		if (equipObjectVariant != "") {
			StringUtil::replaceAll(description, "{type}", equipObjectVariant);
		}
		return description;		
	}
	Graph::StateData& getState() {
		if (currentState == "") {
			return node->states.begin()->second;
		}
		return node->states.at(currentState);
	}
};

struct MenuPage {
	Graph::MenuNode* node;	
	GameAPI::GameActor* actor = nullptr;
	MenuPage* parent = nullptr;
	std::vector<MenuOption> options;
	std::vector<MenuPage> subPages;
	std::string getDescription() {
		std::string description = node->description;
		if (actor) {
			StringUtil::replaceAll(description, "{actor}", actor->getName());
		}
		return description;
	}
};

namespace UI::Scene{
	enum HandleResult{
		kExit,
		kHandled
	};

	class SceneOptions : public Threading::ISingleton<SceneOptions> {
	public:
		HandleResult Handle(int i);
		void ExecuteOption(MenuOption& option);
		void BuildMenuData(MenuData& menuData);
		void BuildPageTree();
		inline bool isAtRoot() const { return selectedPage == nullptr; }
	private:
		void BuildSubPages(MenuPage* parent, std::vector<MenuPage>& subPages, std::vector<Graph::MenuNode>& inputPages);
		void BuildOptions(MenuPage* parent, Graph::MenuNode& page);
		Graph::MenuNode* selectedNode = &Graph::GraphTable::getRootOptionsNode();
		MenuPage* selectedPage = nullptr;
		std::vector<MenuPage> rootPages;
	};
}