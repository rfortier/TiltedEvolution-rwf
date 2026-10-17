#pragma once

// Dual-mode launch routing for the dedicated server.
//
// Everything in this header is a pure function of its inputs (plus the
// environment probe IsGraphicalEnvironment). No globals, no exit() calls, so
// main() stays testable and the decision can be logged before anything heavy
// (server dll, logger, uv loop) is touched.

#include <cstddef>

namespace serverui
{
enum class LaunchMode
{
    Cli,            // classic terminal server: console owned by us
    Gui,            // Win32 control panel: never keep a console window
    Headless,       // no console interaction at all, logging goes to file
    GenerateConfig, // one shot: write the default ini and exit
};

// Human readable name, for logs and for the tests.
const char* ToString(LaunchMode aMode) noexcept;

// Parses the ini value ("cli", "gui", "headless", "generate-config").
// Returns false when the text is not a known mode; never throws.
bool ParseLaunchMode(const char* acText, LaunchMode* apOut) noexcept;

// Precedence, first match wins:
//   1. --cli | --console | --nogui    -> Cli
//   2. --headless                     -> Headless
//   3. --gui                          -> Gui
//   4. --generate-config              -> GenerateConfig
//   5. config file key launch/mode=<mode>
//   6. Gui when IsGraphicalEnvironment() reports a reachable display,
//      otherwise Cli. Same rule on Windows and Linux.
//
// Step 6 is an *environment* probe, evaluated once at startup. It can answer
// "a display exists" while the window still fails to open (broken X forwarding,
// no usable GL context, a compositor refusing a surface). There is no runtime
// fallback: the host reports the failure and the process exits non-zero, so a
// deployment never silently runs in a mode other than the one it selected.
// --cli / --console / --nogui and --headless are the explicit ways to ask for
// the console.
// acConfigPath may be null, in which case step 5 is skipped.
LaunchMode ResolveLaunchMode(int argc, char** argv, const char* acConfigPath);

// Conservative "can a window actually be shown here?" probe.
// Returns false for services, session 0, disconnected RDP sessions and for any
// host without a reachable display server. Documented per check in the .cpp.
bool IsGraphicalEnvironment() noexcept;
} // namespace serverui
