#pragma once

// DashboardImGui: the draw layer.
//
// ===========================================================================
// NOT COMPILE-VERIFIED ON THE DEVELOPMENT HOST.
// imgui.h is an xmake package and is not vendored in this repository, so this
// translation unit cannot be built here. It is written against the public
// ImGui API (docking branch, v1.90-era signatures) and must be compiled where
// the package resolves. Everything this layer *decides* lives in
// DashboardModel / Sparkline, which are compile-verified and test-covered; what
// remains here is widget plumbing.
// ===========================================================================
//
// This header deliberately includes no ImGui type: the Win32/D3D11 host only
// needs the three free functions, and keeping <imgui.h> inside the .cpp keeps
// the host's include graph free of the package that is missing here.

#include "DashboardModel.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace serverui
{

// The commands the dashboard issues to the core. The host fills these in; the
// draw layer never talks to ServerCore itself, so it stays testable-by-reading.
struct DashboardActions
{
    std::function<void()> start;
    std::function<void()> restart;
    std::function<void()> gracefulShutdown;
    std::function<void(std::string_view)> submitCommand;
    std::function<void(uint32_t aConnectionId)> kickPlayer;
    std::function<void(uint32_t aConnectionId)> banPlayer;
    std::function<void(uint32_t aConnectionId)> sendDirectMessage;
    // Settings panel: called with the edited values on "Apply".
    std::function<void(uint16_t aPort, uint32_t aMaxPlayers, uint32_t aTickRate, LogLevel aLogLevel)> applySettings;

    // Called once per frame, on the UI thread, before anything is drawn.
    //
    // This is where the caller refreshes the model from its own data sources.
    // It exists because the model is NOT thread safe: refreshing it from a
    // background thread while the draw code reads it is a data race, and the
    // only safe place to touch it is the frame that reads it.
    std::function<void(DashboardModel&)> beforeFrame;
};

// Applies the dashboard's dark theme to the current ImGui context. Call once
// after ImGui::CreateContext, before the first frame.
void ApplyDashboardTheme();

// Draws one whole frame. aClientWidth/aClientHeight are the host's client area
// in pixels; the layer lays the four panels out inside it.
// Returns true when the console input still holds keyboard focus, so the host
// knows to swallow key events it would otherwise handle as shortcuts.
bool DrawDashboard(DashboardModel& aModel, const DashboardActions& acActions,
    float aClientWidth, float aClientHeight);

// Same, but for the settings panel only (the host may show it in a separate
// window or an ImGui popup).
void DrawConfigPanel(DashboardModel& aModel, const DashboardActions& acActions);

} // namespace serverui
