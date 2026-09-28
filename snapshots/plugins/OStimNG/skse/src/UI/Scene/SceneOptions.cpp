#include "SceneOptions.h"
#include "UI/UIState.h"
#include <Graph/GraphTable.h>
#include <Util/UIUtil.h>
#include <Util/StringUtil.h>
#include <Trait/TraitTable.h>


namespace UI::Scene {

	HandleResult SceneOptions::Handle(int i) {
		auto& rootNode = Graph::GraphTable::getRootOptionsNode();
		if (i == -1) {
			if (selectedPage == nullptr) {
				return kExit;
			}
			else if (selectedPage->parent == nullptr) {
				selectedPage = nullptr;
			}
			else {
				selectedPage = selectedPage->parent;
			}
		}
		else if (selectedPage == nullptr) {
			selectedPage = &rootPages[i];
		}
		else if (i < selectedPage->subPages.size()) {
			selectedPage = &selectedPage->subPages[i];
		}
		else {
			MenuOption& ptr = selectedPage->options[i - selectedPage->subPages.size()];
			ExecuteOption(ptr);
		}
		return kHandled;
	}

	void SceneOptions::ExecuteOption(MenuOption& option){
		auto& state = option.getState();
		const auto skyrimVM = RE::SkyrimVM::GetSingleton();
		auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
		if (vm) {
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			
			RE::BSScript::IFunctionArguments* args = nullptr;
			if (option.actor != nullptr) {				
				if (option.equipObjectVariant != "") {
					auto stateVal = option.currentState;
					auto equipObjectVariant = option.equipObjectVariant;
					args = RE::MakeFunctionArguments(std::move(stateVal), std::move(option.actor->form), std::move(equipObjectVariant));
				}
				else {
					auto stateVal = option.currentState;
					args = RE::MakeFunctionArguments(std::move(stateVal), std::move(option.actor->form));
				}
			}
			else {
				auto stateVal = option.currentState;
				args = RE::MakeFunctionArguments(std::move(stateVal));
			}

			vm->DispatchStaticCall(state.script, state.function, args, callback);
		}
	}

	void SceneOptions::BuildPageTree() {
		// Clear old tree to prevent dangling pointers
		selectedPage = nullptr;
		rootPages.clear();
		
		auto& rootSubnodes = Graph::GraphTable::getRootOptionsNode().subNodes;
		BuildSubPages(nullptr, rootPages, rootSubnodes);
	}

	void SceneOptions::BuildSubPages(MenuPage* parent, std::vector<MenuPage>& subPages, std::vector<Graph::MenuNode>& inputPages) {
		for (auto& page : inputPages) {
			if (page.repeat == "actor") {
				auto& thread = UI::UIState::GetSingleton()->currentThread;
				for (auto& actorKvP : thread->getActors()) {
					MenuPage newPage;
					newPage.node = &page;
					newPage.actor = &actorKvP.second.getActor();
					newPage.parent = parent;
					auto& subNodes = page.subNodes;
					subPages.push_back(newPage);
					BuildSubPages(&subPages[subPages.size() - 1], subPages[subPages.size() - 1].subPages, subNodes);
					BuildOptions(&subPages[subPages.size() - 1], page);
				}
			} else {
				MenuPage newPage;
				newPage.node = &page;
				newPage.actor = nullptr;
				auto& subNodes = page.subNodes;
				newPage.parent = parent;
				subPages.push_back(newPage);
				BuildSubPages(&subPages[subPages.size() - 1], subPages[subPages.size() - 1].subPages, subNodes);
				BuildOptions(&subPages[subPages.size() - 1], page);
			}
		}
	}

	void SceneOptions::BuildOptions(MenuPage* parent, Graph::MenuNode& page) {
		for (auto& option : page.options) {
			if (option.repeat == "actor") {
				auto& thread = UI::UIState::GetSingleton()->currentThread;
				for (auto& actorKvP : thread->getActors()) {
					MenuOption newOption;
					newOption.node = &option;
					newOption.actor = &actorKvP.second.getActor();
					newOption.parent = parent;
					parent->options.push_back(newOption);
				}
			}
			else if (option.repeat == "equipObject") {
				if (option.equipType != "") {
					MenuOption toggleOption;
					toggleOption.node = &option;
					toggleOption.actor = parent->actor;
					toggleOption.parent = parent;
					toggleOption.equipObjectVariant = "";
					toggleOption.currentState = "toggle";
					parent->options.push_back(toggleOption);

					for (auto& equipObject : Trait::TraitTable::getEquipObjectsOfType(option.equipType)) {
						MenuOption newOption;
						newOption.node = &option;
						newOption.actor = parent->actor;
						newOption.parent = parent;
						newOption.equipObjectVariant = equipObject;
						newOption.currentState = "object";
						parent->options.push_back(newOption);
					}
				}				
			}
			else {
				MenuOption newOption;
				newOption.node = &option;
				newOption.actor = parent->actor;
				newOption.parent = parent;
				parent->options.push_back(newOption);
			}
		}
	}
	
	void SceneOptions::BuildMenuData(MenuData& menuData) {
		menuData.options.clear();

		if (selectedPage == nullptr) {
			menuData.options.push_back({
				.nodeId = "-1",
				.title = "exit",
				.imagePath = UI::getIconTexture("OStim/symbols/x"),
				.border = "ff0000",
				.description = "Return to Navigation"
				});			
		}
		else {
			menuData.options.push_back({
				.nodeId = "-1",
				.title = "back",
				.imagePath = UI::getIconTexture("OStim/symbols/return"),
				.border = "ff0000",
				.description = "Return to " + (selectedPage->parent != nullptr ? selectedPage->parent->node->name : "Options")
				});			
		}
		std::vector<MenuPage>* currentPages = (selectedPage == nullptr ? &rootPages : &selectedPage->subPages);
		for (int i = 0; i < currentPages->size(); i++) {
			auto node = currentPages->at(i).node;
			menuData.options.push_back({
				.nodeId = std::to_string(i),
				.title = node->name,
				.imagePath = node->icon,
				.border = node->border,
				.description = currentPages->at(i).getDescription()
				});
		}
		if (selectedPage != nullptr) {
			for (int i = 0; i < selectedPage->options.size(); i++) {
				auto& state = selectedPage->options[i].getState();
				menuData.options.push_back({
								.nodeId = std::to_string(i + currentPages->size()),
								.title = state.name,
								.imagePath = state.icon,
								.border = state.border,
								.description = selectedPage->options[i].getDescription()
					});
			}
		}		
	}
}