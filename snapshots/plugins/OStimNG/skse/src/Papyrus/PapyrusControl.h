#pragma once

#include <UI/UIInterop.h>
#include <UI/UIState.h>

namespace PapyrusControl {
    using VM = RE::BSScript::IVirtualMachine;

    void Control(RE::StaticFunctionTag*, UI::Controls a_direction, int64_t a_glyph) {
        UI::UIState::GetSingleton()->HandleControl(a_direction);
    }

    void EndPlayerDialogue(RE::StaticFunctionTag*) {
        RE::PlayerCharacter::GetSingleton()->EndDialogue();
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OSANative"sv;

        BIND(Control);
        BIND(EndPlayerDialogue);

        return true;
    }
}  // namespace PapyrusControl