#include "LaunchMode.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>

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
// Case insensitive compare for the SESSIONNAME check; avoids the _stricmp /
// strcasecmp portability split.
bool EqualsIgnoreCase(std::string_view aLeft, std::string_view aRight) noexcept
{
    if (aLeft.size() != aRight.size())
        return false;

    for (size_t i = 0; i < aLeft.size(); ++i)
    {
        char lhs = aLeft[i];
        char rhs = aRight[i];
        if (lhs >= 'A' && lhs <= 'Z')
            lhs = static_cast<char>(lhs - 'A' + 'a');
        if (rhs >= 'A' && rhs <= 'Z')
            rhs = static_cast<char>(rhs - 'A' + 'a');
        if (lhs != rhs)
            return false;
    }

    return true;
}

bool HasFlag(int argc, char** argv, const char* acFlag) noexcept
{
    if (argv == nullptr)
        return false;

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] != nullptr && std::strcmp(argv[i], acFlag) == 0)
            return true;
    }

    return false;
}

std::string_view Trim(std::string_view aText) noexcept
{
    constexpr char kWhitespace[] = " \t\r\n";
    const size_t first = aText.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos)
        return {};

    const size_t last = aText.find_last_not_of(kWhitespace);
    return aText.substr(first, last - first + 1);
}

std::string_view StripQuotes(std::string_view aText) noexcept
{
    if (aText.size() >= 2 && (aText.front() == '"' || aText.front() == '\'') && aText.back() == aText.front())
        return aText.substr(1, aText.size() - 2);

    return aText;
}

// Reads launch/mode out of the ini. Both shapes used in this repository are
// accepted: the sectioned "[launch] / mode = gui" and the flat
// "launch/mode = gui". A present key with an unknown value is not silently
// upgraded to another key: it simply yields "no config answer".
bool ReadModeFromConfig(const char* acPath, LaunchMode* apOut) noexcept
{
    if (acPath == nullptr || *acPath == '\0' || apOut == nullptr)
        return false;

    std::ifstream file(acPath);
    if (!file.is_open())
        return false;

    std::string section;
    std::string line;
    while (std::getline(file, line))
    {
        const std::string_view view = Trim(line);
        if (view.empty() || view.front() == ';' || view.front() == '#')
            continue;

        if (view.front() == '[')
        {
            const size_t close = view.find(']');
            if (close != std::string_view::npos)
                section.assign(view.substr(1, close - 1));
            continue;
        }

        const size_t equals = view.find('=');
        if (equals == std::string_view::npos)
            continue;

        const std::string_view key = Trim(view.substr(0, equals));
        const std::string_view value = StripQuotes(Trim(view.substr(equals + 1)));
        const bool isModeKey = (key == "launch/mode") || (key == "mode" && section == "launch");
        if (!isModeKey)
            continue;

        LaunchMode parsed{};
        if (ParseLaunchMode(std::string(value).c_str(), &parsed))
        {
            *apOut = parsed;
            return true;
        }

        return false;
    }

    return false;
}

#ifdef _WIN32
// Check 1 + 2: a service session is bound to a non visible window station
// ("Service-0x0-3e7$"), and a session without an attached interactive desktop
// (session 0, disconnected RDP, locked out console) cannot open an input
// desktop. Both are hard "no GUI here" answers.
bool HasInteractiveWindowStation() noexcept
{
    const HWINSTA windowStation = GetProcessWindowStation();
    if (windowStation == nullptr)
        return false;

    USEROBJECTFLAGS flags{};
    DWORD bytesNeeded = 0;
    if (GetUserObjectInformationA(windowStation, UOI_FLAGS, &flags, sizeof(flags), &bytesNeeded) == 0)
        return false;

    if ((flags.dwFlags & WSF_VISIBLE) == 0)
        return false;

    const HDESK inputDesktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (inputDesktop == nullptr)
        return false;

    CloseDesktop(inputDesktop);
    return true;
}

// Check 3: belt and braces for the service case; SESSIONNAME is "Services"
// inside session 0 even when the window station probe somehow succeeds.
bool IsServiceSession() noexcept
{
    char buffer[64]{};
    const DWORD length = GetEnvironmentVariableA("SESSIONNAME", buffer, static_cast<DWORD>(sizeof(buffer)));
    if (length == 0 || length >= sizeof(buffer))
        return false;

    return EqualsIgnoreCase(std::string_view(buffer, length), "Services");
}
#endif
} // namespace

const char* ToString(LaunchMode aMode) noexcept
{
    switch (aMode)
    {
    case LaunchMode::Cli: return "cli";
    case LaunchMode::Gui: return "gui";
    case LaunchMode::Headless: return "headless";
    case LaunchMode::GenerateConfig: return "generate-config";
    }

    return "unknown";
}

bool ParseLaunchMode(const char* acText, LaunchMode* apOut) noexcept
{
    if (acText == nullptr || apOut == nullptr)
        return false;

    if (std::strcmp(acText, "cli") == 0 || std::strcmp(acText, "console") == 0)
    {
        *apOut = LaunchMode::Cli;
        return true;
    }

    if (std::strcmp(acText, "gui") == 0)
    {
        *apOut = LaunchMode::Gui;
        return true;
    }

    if (std::strcmp(acText, "headless") == 0)
    {
        *apOut = LaunchMode::Headless;
        return true;
    }

    if (std::strcmp(acText, "generate-config") == 0 || std::strcmp(acText, "generateconfig") == 0)
    {
        *apOut = LaunchMode::GenerateConfig;
        return true;
    }

    return false;
}

bool IsGraphicalEnvironment() noexcept
{
#ifdef _WIN32
    if (IsServiceSession())
        return false;

    return HasInteractiveWindowStation();
#else
    // Check 1: a display server must be reachable, otherwise there is no
    // surface to draw on.
    const char* display = std::getenv("DISPLAY");
    if (display != nullptr && *display != '\0')
        return true;

    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (waylandDisplay != nullptr && *waylandDisplay != '\0')
        return true;

    // Check 2: neither variable is set. Stdin interactivity is the only signal
    // left at that point, and a terminal can only ever argue for CLI: a tty
    // does not conjure a display server. Both the tty and the redirected case
    // therefore answer false, which keeps this probe conservative.
    return false;
#endif
}

LaunchMode ResolveLaunchMode(int argc, char** argv, const char* acConfigPath)
{
    // Argument precedence is by category, not by position on the command line.
    if (HasFlag(argc, argv, "--cli") || HasFlag(argc, argv, "--console") || HasFlag(argc, argv, "--nogui"))
        return LaunchMode::Cli;

    if (HasFlag(argc, argv, "--headless"))
        return LaunchMode::Headless;

    if (HasFlag(argc, argv, "--gui"))
        return LaunchMode::Gui;

    if (HasFlag(argc, argv, "--generate-config"))
        return LaunchMode::GenerateConfig;

    LaunchMode fromConfig{};
    if (ReadModeFromConfig(acConfigPath, &fromConfig))
        return fromConfig;

    // One rule for both platforms: default to the GUI when a display can
    // actually be reached, otherwise the console.
    //
    // Linux has a real GUI now (GLFW + OpenGL3 + ImGui), so the old
    // "never default to GUI off Windows" rule is gone. IsGraphicalEnvironment()
    // stays deliberately conservative - it answers false for a bare VPS with no
    // X server, and false for a service session on Windows - so a headless box
    // still gets the CLI with no configuration.
    //
    // This decides ONCE, at startup, from the environment. It cannot know
    // whether the window will really open (broken X forwarding, a driver
    // refusing a GL context, Wayland without the right protocol), and it does
    // not try: if the window then fails, the server reports that and exits
    // non-zero rather than switching modes behind the operator's back. Run with
    // --cli or --headless to select the console deliberately on such a host.
    return IsGraphicalEnvironment() ? LaunchMode::Gui : LaunchMode::Cli;
}
} // namespace serverui
