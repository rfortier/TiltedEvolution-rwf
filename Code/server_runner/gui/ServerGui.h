// Minimal native Win32 control panel for the dedicated server.
// Deliberately dependency free (user32/gdi32 only) so it cannot break the
// server build; it runs the existing console loop on a background thread and
// surfaces status, player count and log tail in a single small window.
//
// Non-Windows or --nogui builds keep the plain console behavior.

#pragma once

#include <string>

namespace server_gui
{
// Returns false when a GUI cannot/should not be created (non-Windows,
// headless); the caller then falls back to the console terminal io.
bool RequestGuiMode(int argc, char** argv);

// Creates the window on the calling thread and pumps messages until quit.
// Returns the process exit code.
int RunGui(int argc, char** argv);
} // namespace server_gui
