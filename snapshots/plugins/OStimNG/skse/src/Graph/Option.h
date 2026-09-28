#pragma once

namespace Graph {

	struct OptionData {
		std::string name;
		std::string description;
		std::string icon;
		std::string border = "ffffff";
	};
	struct OptionNode;
	struct MenuNode : OptionData {
		std::vector<MenuNode> subNodes;
		std::vector<OptionNode> options;
		std::string parentName;
		MenuNode* parent = nullptr;
		std::string repeat;
		std::string equipType;
	};
	struct StateData : OptionData {
		std::string script;
		std::string function;
	};
	struct OptionNode :MenuNode {
		std::unordered_map<std::string, StateData> states;
		std::string stateFaction = "";
	};
}