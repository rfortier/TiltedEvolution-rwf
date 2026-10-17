#pragma once

// DashboardHostLinux: GLFW + OpenGL3 + Dear ImGui host for the dashboard.
//
// ===========================================================================
// NOT COMPILE-VERIFIED ON THE DEVELOPMENT HOST.
// glfw and imgui are xmake packages and neither is present here, so this file
// cannot be built on the machine it was written on. Everything it *decides*
// lives in DashboardModel (compile-verified and test-covered); what remains here
// is window creation and per-frame ImGui plumbing.
// ===========================================================================
//
// This header includes no GLFW or ImGui type, so main() can call it without
// dragging either package into every translation unit's include graph.
//
// SCOPE: Linux only. The Windows host lives in DashboardHost.h and is not
// interchangeable with this one - that one owns a D3D11 swap chain.

#include <cstdint>

namespace serverui
{
class DashboardModel;
}

namespace server_runner
{

// Actions the host wires from the dashboard to the core. Layout-identical to the
// struct in DashboardHost.h, deliberately re-declared rather than shared so that
// including this header never drags in the D3D11 host on a platform that has no
// D3D11.
struct DashboardHostActionsLinux
{
    void (*gracefulShutdown)(void*) = nullptr;
    void (*submitCommand)(void*, const char*) = nullptr;
    void* userData = nullptr;

    // See DashboardHostActions::beforeFrame: a per-frame refresh hook, called on
    // the UI thread before the model is drawn.
    void (*beforeFrame)(serverui::DashboardModel&) = nullptr;
};

// Returned when no window could be opened: broken X forwarding, no GL context,
// a compositor refusing a surface. The server does NOT silently drop to the
// console on this - the requested mode failed and the process reports it and
// exits non-zero, so a deployment notices instead of quietly running in a
// different mode than the one it asked for. Use --cli/--headless to run without
// a window deliberately.
constexpr int kDashboardWindowUnavailable = 1;

// Opens the dashboard window and pumps frames until it is closed.
// aModel is filled by the caller between frames; the caller owns the core.
// Returns 0 on a normal close, kDashboardWindowUnavailable when no window could
// be created.
int RunDashboardWindowLinux(serverui::DashboardModel& aModel, const DashboardHostActionsLinux& acActions,
    const char* apTitle, int aWidth, int aHeight);

} // namespace server_runner
