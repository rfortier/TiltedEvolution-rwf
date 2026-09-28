#pragma once

#include "ScriptAPI/PlayerThreadScript.h"

namespace PapyrusPlayerThread {
	using VM = RE::BSScript::IVirtualMachine;

	void SetPlayerControl(RE::StaticFunctionTag*, bool control) {
        ScriptAPI::PlayerThread::setPlayerControl(control);
	}

	bool Bind(VM* a_vm) {
		const auto obj = "OPlayerThread"sv;

		BIND(SetPlayerControl);

		return true;
	}
}