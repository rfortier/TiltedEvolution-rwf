#include "ConsoleHost.h"

#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace serverui
{
namespace
{
#ifdef _WIN32
// Tracks what THIS process did, so ReleaseConsole only undoes our own work and
// a second EnsureConsoleForMode call does not double allocate.
struct ConsoleState
{
    bool ownsConsole{false}; // we called AllocConsole
    bool stdStreamsBound{false};
};

ConsoleState& State() noexcept
{
    // Function local static: no global init order problem, and the two booleans
    // are only ever touched from the thread that drives startup/shutdown.
    static ConsoleState s_state;
    return s_state;
}

// A stream whose OS handle is already valid must be left alone. That is what
// keeps "app.exe --cli > log.txt" and "app.exe --cli | more" working: rebinding
// those onto CONOUT$ would silently discard the redirection and send the log to
// a window nobody asked for. Only the genuinely absent handle - the normal case
// for a /SUBSYSTEM:WINDOWS binary, whose std handles are NULL at startup - gets
// reopened.
bool StreamNeedsBinding(DWORD aStdHandle) noexcept
{
    const HANDLE handle = GetStdHandle(aStdHandle);
    return handle == nullptr || handle == INVALID_HANDLE_VALUE;
}

// After AllocConsole/AttachConsole the CRT's std streams still point at the
// handles captured at load time (which are invalid for a windowed subsystem
// binary). Reopening them on CONIN$/CONOUT$ is what makes printf and
// std::cout work; spdlog's stdout_color_sink_mt writes to stdout too.
void BindStandardStreams() noexcept
{
    FILE* pStream = nullptr;

    // freopen_s is the MSVC CRT form; MinGW also provides it via the CRT
    // compatibility layer, and it is the documented way to reuse the FILE.
    if (StreamNeedsBinding(STD_OUTPUT_HANDLE))
    {
        if (freopen_s(&pStream, "CONOUT$", "w", stdout) != 0)
            (void)std::freopen("CONOUT$", "w", stdout);
    }

    if (StreamNeedsBinding(STD_ERROR_HANDLE))
    {
        if (freopen_s(&pStream, "CONOUT$", "w", stderr) != 0)
            (void)std::freopen("CONOUT$", "w", stderr);
    }

    if (StreamNeedsBinding(STD_INPUT_HANDLE))
    {
        if (freopen_s(&pStream, "CONIN$", "r", stdin) != 0)
            (void)std::freopen("CONIN$", "r", stdin);
    }

    // The runner's own ConfigureConsoleMode() does the same thing; doing it
    // here too means a freshly allocated console is UTF-8 before the first log
    // line is written.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::clearerr(stdin);
    std::clearerr(stdout);
    std::clearerr(stderr);

    State().stdStreamsBound = true;
}

void DetachFromConsole() noexcept
{
    if (GetConsoleWindow() != nullptr || State().ownsConsole)
        FreeConsole();

    State().ownsConsole = false;
    State().stdStreamsBound = false;
}
#endif
} // namespace

bool HasConsole() noexcept
{
#ifdef _WIN32
    return GetConsoleWindow() != nullptr;
#else
    return false;
#endif
}

void EnsureConsoleForMode(LaunchMode aMode) noexcept
{
#ifdef _WIN32
    switch (aMode)
    {
    case LaunchMode::Cli: {
        // Already have one: either we allocated it on an earlier call, or the
        // process was started from a console (a /SUBSYSTEM:CONSOLE build).
        if (HasConsole())
        {
            if (!State().stdStreamsBound)
                BindStandardStreams();
            return;
        }

        // Prefer the parent's console: "app.exe --cli" from cmd.exe must write
        // into that window instead of spawning a second one.
        if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
        {
            // No parent console (double click, shortcut, scheduled task).
            if (AllocConsole() == 0)
                return; // nothing more we can do; file logging still works
            State().ownsConsole = true;
        }

        BindStandardStreams();
        return;
    }

    case LaunchMode::Gui:
        // A GUI launch must never leave a console behind, whichever way the
        // process got one.
        DetachFromConsole();
        return;

    case LaunchMode::Headless:
        // No console interaction at all. Do not allocate, do not attach, and do
        // not detach either: if a console was handed to us by the parent we
        // simply never write to it. Logging is file based.
        return;

    case LaunchMode::GenerateConfig:
        // Same as Cli: the user needs to see what was written and where.
        if (!HasConsole() && AttachConsole(ATTACH_PARENT_PROCESS) == 0)
        {
            if (AllocConsole() == 0)
                return;
            State().ownsConsole = true;
        }

        if (!State().stdStreamsBound)
            BindStandardStreams();
        return;
    }
#else
    // No console concept to manage: the terminal owns the process std streams
    // already, and a GUI mode on this platform is not shipped.
    (void)aMode;
#endif
}

void ReleaseConsole() noexcept
{
#ifdef _WIN32
    // Only release what we allocated; an attached parent console belongs to the
    // parent and freeing it would pull the window out from under cmd.exe.
    if (State().ownsConsole)
    {
        FreeConsole();
        State().ownsConsole = false;
        State().stdStreamsBound = false;
    }
#else
    // Nothing acquired, nothing to release.
#endif
}
} // namespace serverui
