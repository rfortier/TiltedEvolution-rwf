#pragma once

#include <ModAPI/OstimNG-API-Thread.h>

namespace UI {
    using Controls = OstimNG_API::Thread::Controls;

    inline static RE::GPtr<RE::IMenu> GetHud() {
        auto ui = RE::UI::GetSingleton();
        return ui->GetMenu("HUD Menu");
    }
    inline static RE::GFxValue GetOSAControlUIRoot(RE::GPtr<RE::IMenu> a_menu, int64_t a_glyph) {
        std::string container = "_root.WidgetContainer." + std::to_string(a_glyph) + ".widget.ctr";
        RE::GFxValue root;
        if (a_menu) {
            a_menu->uiMovie->GetVariable(&root, container.c_str());
        }
        return root;
    }

    inline static std::string GetControlString(Controls a_control) {
        switch (a_control) {
            case Controls::Up:
                return "UP";
            case Controls::Down:
                return "DOWN";
            case Controls::Left:
                return "LEFT";
            case Controls::Right:
                return "RIGHT";
            case Controls::Toggle:
                return "TOG";
            case Controls::Yes:
                return "YES";
            case Controls::No:
                return "NO";
            case Controls::Menu:
                return "MENU";
            default:
                return "";
        }
    }
}  // namespace UI