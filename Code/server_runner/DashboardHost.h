#pragma once

// DashboardHost: Win32 + D3D11 + Dear ImGui host for the dashboard.
//
// ===========================================================================
// NOT COMPILE-VERIFIED ON THE DEVELOPMENT HOST.
// imgui.h is an xmake package and is not vendored here, so this file cannot be
// built on a host without it. Everything it *decides* lives in DashboardModel,
// which is compile-verified and test-covered; this file only owns the window,
// the swap chain and the per-frame ImGui plumbing.
// ===========================================================================
//
// This header includes no ImGui or D3D11 type, so main() can call it without
// dragging the package into the include graph of every translation unit.

#include <cstdint>
#include <string>

namespace serverui
{
class DashboardModel;
}

namespace server_runner
{

// Actions the host wires from the dashboard to the core. Declared here rather
// than taken from DashboardImGui.h so this header stays free of the draw layer.
struct DashboardHostActions
{
    void (*gracefulShutdown)(void*) = nullptr;
    void (*submitCommand)(void*, const char*) = nullptr;
    void* userData = nullptr;

    // Called once per frame on the UI thread, before anything is drawn, so the
    // caller refreshes the model where it is consumed. The model passed in is
    // the same one the draw layer is about to read.
    void (*beforeFrame)(serverui::DashboardModel&) = nullptr;
};

// Runs the dashboard window on the calling thread until it is closed.
// aModel is driven by the caller between frames (the caller owns the core).
// Returns the process exit code: 0 on a normal close.
//
// Returns 1 without creating a window when the D3D11 device or swap chain
// cannot be created, so the caller can fall back to a console run instead of
// silently doing nothing.
int RunDashboardWindow(serverui::DashboardModel& aModel, const DashboardHostActions& acActions,
    const wchar_t* apTitle, int aWidth, int aHeight);

} // namespace server_runner
