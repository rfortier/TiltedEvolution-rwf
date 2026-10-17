#pragma once

// Console lifetime control for a /SUBSYSTEM:WINDOWS binary.
//
// A windowed-subsystem process starts with NO console at all, so every
// printf/std::cout writes into the void and a CLI user sees nothing. This is
// the mechanism that fixes it without bringing the black window back for GUI
// users:
//
//   * Cli mode       AttachConsole(ATTACH_PARENT_PROCESS) first, so that
//                    "app.exe --cli" typed into an existing cmd.exe writes into
//                    THAT window. Only when the parent has no console do we
//                    AllocConsole() and own a new one.
//   * Gui mode       FreeConsole() if anything is attached, so a double click
//                    never leaves a stray window behind.
//   * Headless mode  never touches the console; logs go to file only.
//
// Every entry point is idempotent and a no-op outside Windows.

#include "LaunchMode.h"

namespace serverui
{
// Brings the process console into the state required by aMode and rebinds the
// C std streams (stdin/stdout/stderr) onto CONIN$/CONOUT$ so that printf,
// std::cout and spdlog's stdout sink reach the window.
//
// Safe to call more than once, from any thread, and before or after logging
// has started. Never throws.
void EnsureConsoleForMode(LaunchMode aMode) noexcept;

// True when this process currently owns or is attached to a console.
bool HasConsole() noexcept;

// Restores the console to its original state and releases anything acquired by
// EnsureConsoleForMode. Intended for the last line of main(). Never throws.
void ReleaseConsole() noexcept;
} // namespace serverui
