#pragma once

#include "Graph/GraphTable.h"

namespace PapyrusEvent {
	using VM = RE::BSScript::IVirtualMachine;

	bool IsChildOf(RE::StaticFunctionTag*, std::string superType, std::string subType) {
        Graph::Event* superEvent = Graph::GraphTable::getEvent(superType);
        Graph::Event* subEvent = Graph::GraphTable::getEvent(subType);

		return superEvent && subEvent && subEvent->isChildOf(superEvent);
	}


	bool Bind(VM* a_vm) {
		const auto obj = "OEvent"sv;

		BIND(IsChildOf);

		return true;
	}
}