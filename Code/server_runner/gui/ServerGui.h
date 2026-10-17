// Minimal native Win32 control panel for the dedicated server.
// Deliberately dependency free (user32/gdi32 only) so it cannot break the
// server build; it runs the existing console loop on a background thread and
// surfaces status, player count and log tail in a single small window.
//
// Non-Windows or --nogui builds keep the plain console behavior.

#pragma once

#include <string>

namespace serverui
{
class ServerCore;
}

namespace server_gui
{
// Returns false when a GUI cannot/should not be created (non-Windows,
// headless); the caller then falls back to the console terminal io.
bool RequestGuiMode(int argc, char** argv);

// Legacy path: builds and owns its own DediRunner on a background thread.
// Kept for builds without server_ui. Do NOT combine with RunGuiWithCore.
int RunGui(int argc, char** argv);

// Dashboard path: shows the window and reads status from an already started
// core. The core is owned by the caller, which also owns the server lifetime,
// so there is exactly one server in the process. Returns the exit code.
int RunGuiWithCore(int argc, char** argv, serverui::ServerCore& aCore);
} // namespace server_gui
