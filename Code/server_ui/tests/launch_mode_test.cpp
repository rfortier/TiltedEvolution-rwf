// Standalone test for LaunchMode resolution. No test framework: plain asserts
// so the file can be built and run with a single g++ command.
//
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -o launch_mode_test.exe ^
//       tests/launch_mode_test.cpp src/launch/LaunchMode.cpp

#include "../src/launch/LaunchMode.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using serverui::LaunchMode;
using serverui::ResolveLaunchMode;

namespace
{
int g_failures = 0;

void Check(bool aCondition, const char* acWhat)
{
    if (aCondition)
        return;

    std::printf("FAIL: %s\n", acWhat);
    ++g_failures;
}

// Resolves from a mutable argv, so the table below reads as data.
LaunchMode Resolve(std::vector<const char*> aArgs, const char* acConfigPath = nullptr)
{
    // argv[0] is the program name, as main() would deliver it.
    aArgs.insert(aArgs.begin(), "app.exe");
    return ResolveLaunchMode(static_cast<int>(aArgs.size()), const_cast<char**>(aArgs.data()), acConfigPath);
}

std::string WriteTempIni(const char* acName, const std::string& acBody)
{
    const auto path = std::filesystem::temp_directory_path() / acName;
    std::ofstream file(path, std::ios::trunc);
    file << acBody;
    file.close();
    return path.string();
}

// The documented default is now platform independent: GUI when a display is
// reachable, CLI otherwise. Linux gained a real GUI (GLFW + OpenGL3), so the old
// "never GUI off Windows" rule is gone and this helper mirrors ResolveLaunchMode
// exactly instead of special-casing the platform.
LaunchMode DefaultMode()
{
    return serverui::IsGraphicalEnvironment() ? LaunchMode::Gui : LaunchMode::Cli;
}

// Proves the default is decided by a startup environment probe rather than a
// hardcoded platform answer, and that explicit flags always override it.
// DISPLAY and WAYLAND_DISPLAY are the only two signals the probe reads off
// Windows, so manipulating them is a complete test of the non-Windows branch.
//
// Note what is NOT tested here: the absence of a runtime fallback. That is a
// property of main.cpp (a failed window returns non-zero instead of switching
// modes), not of mode resolution, and it cannot be observed from this binary.
void TestDefaultFollowsDisplay()
{
#if defined(_WIN32)
    std::printf("  (skipped on Windows: the probe reads the window station, not DISPLAY)\n");
#else
    // Save whatever the CI environment had.
    const char* savedDisplay = std::getenv("DISPLAY");
    const char* savedWayland = std::getenv("WAYLAND_DISPLAY");
    const std::string displayValue = savedDisplay != nullptr ? savedDisplay : "";
    const std::string waylandValue = savedWayland != nullptr ? savedWayland : "";

    // A reachable display must select the GUI by default.
    setenv("DISPLAY", ":0", 1);
    unsetenv("WAYLAND_DISPLAY");
    Check(DefaultMode() == LaunchMode::Gui, "DISPLAY set: default is Gui");

    // Wayland alone is enough.
    unsetenv("DISPLAY");
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    Check(DefaultMode() == LaunchMode::Gui, "WAYLAND_DISPLAY set: default is Gui");

    // Neither: a bare VPS or a service session gets the console.
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
    Check(DefaultMode() == LaunchMode::Cli, "no display variables: default is Cli");

    // An explicit flag still beats the environment in both directions.
    Check(Resolve({"--nogui"}) == LaunchMode::Cli, "--nogui wins over a reachable display");
    Check(Resolve({"--cli"}) == LaunchMode::Cli, "--cli wins over a reachable display");
    setenv("DISPLAY", ":0", 1);
    Check(Resolve({"--headless"}) == LaunchMode::Headless, "--headless wins over DISPLAY");
    unsetenv("DISPLAY");
    Check(Resolve({"--gui"}) == LaunchMode::Gui, "--gui forces Gui with no display");

    // Restore the environment for whatever runs after this.
    if (!displayValue.empty())
        setenv("DISPLAY", displayValue.c_str(), 1);
    else
        unsetenv("DISPLAY");
    if (!waylandValue.empty())
        setenv("WAYLAND_DISPLAY", waylandValue.c_str(), 1);
    else
        unsetenv("WAYLAND_DISPLAY");
#endif
}

void TestArgumentTable()
{
    // Every documented argument, alone.
    Check(Resolve({"--cli"}) == LaunchMode::Cli, "--cli -> Cli");
    Check(Resolve({"--console"}) == LaunchMode::Cli, "--console -> Cli");
    Check(Resolve({"--nogui"}) == LaunchMode::Cli, "--nogui -> Cli");
    Check(Resolve({"--headless"}) == LaunchMode::Headless, "--headless -> Headless");
    Check(Resolve({"--gui"}) == LaunchMode::Gui, "--gui -> Gui");
    Check(Resolve({"--generate-config"}) == LaunchMode::GenerateConfig, "--generate-config -> GenerateConfig");

    // Precedence is by category, not by position on the command line.
    Check(Resolve({"--headless", "--gui"}) == LaunchMode::Headless, "--headless beats --gui");
    Check(Resolve({"--gui", "--headless"}) == LaunchMode::Headless, "--gui --headless -> Headless");
    Check(Resolve({"--cli", "--headless"}) == LaunchMode::Cli, "--cli beats --headless");
    Check(Resolve({"--headless", "--cli"}) == LaunchMode::Cli, "--headless --cli -> Cli");
    Check(Resolve({"--nogui", "--headless", "--gui"}) == LaunchMode::Cli, "any cli alias wins over all");
    Check(Resolve({"--generate-config", "--gui"}) == LaunchMode::Gui, "--gui beats --generate-config");
    Check(Resolve({"--generate-config", "--headless"}) == LaunchMode::Headless,
        "--headless beats --generate-config");

    // Unknown arguments are not modes and must not change the answer.
    Check(Resolve({"--verbose", "--something=1"}) == DefaultMode(), "unknown args fall through to the default");
    Check(Resolve({}) == DefaultMode(), "no args -> default");

    // A prefix of a known flag is not that flag.
    Check(Resolve({"--cl"}) == DefaultMode(), "--cl is not --cli");
    Check(Resolve({"--headles"}) == DefaultMode(), "--headles is not --headless");
}

void TestConfigFile()
{
    // Sectioned form, as the ini settings provider writes it.
    const std::string sectioned = WriteTempIni("sui_launch_sectioned.ini",
        ";comment\n[launch]\nmode = gui\n");
    Check(Resolve({}, sectioned.c_str()) == LaunchMode::Gui, "config [launch] mode=gui -> Gui");

    // Flat form.
    const std::string flat = WriteTempIni("sui_launch_flat.ini", "launch/mode=headless\n");
    Check(Resolve({}, flat.c_str()) == LaunchMode::Headless, "config launch/mode=headless -> Headless");

    // Quotes, padding and CRLF must all be tolerated.
    const std::string padded = WriteTempIni("sui_launch_padded.ini",
        "\r\n[launch]\r\n  mode   =   \"generate-config\"  \r\n");
    Check(Resolve({}, padded.c_str()) == LaunchMode::GenerateConfig, "quoted + padded config value parses");

    // Comments and unrelated keys are ignored.
    const std::string noisy = WriteTempIni("sui_launch_noisy.ini",
        "# leading comment\n[launch]\nmode = cli\nport = 10578\n[general]\nmode = gui\n");
    Check(Resolve({}, noisy.c_str()) == LaunchMode::Cli, "unrelated keys ignored, [launch] mode wins");

    // An argument always beats the config file.
    Check(Resolve({"--gui"}, sectioned.c_str()) == LaunchMode::Gui, "--gui == config gui");
    Check(Resolve({"--headless"}, sectioned.c_str()) == LaunchMode::Headless, "arg beats config file");
    Check(Resolve({"--generate-config"}, flat.c_str()) == LaunchMode::GenerateConfig, "arg beats config file (2)");

    // Missing / unreadable / malformed config falls through to the default.
    Check(Resolve({}, "Z:/definitely/not/here.ini") == DefaultMode(), "missing config -> default");
    Check(Resolve({}, nullptr) == DefaultMode(), "null config path -> default");

    const std::string unknownValue = WriteTempIni("sui_launch_unknown.ini", "[launch]\nmode = banana\n");
    Check(Resolve({}, unknownValue.c_str()) == DefaultMode(), "unknown config value -> default");

    const std::string empty = WriteTempIni("sui_launch_empty.ini", "");
    Check(Resolve({}, empty.c_str()) == DefaultMode(), "empty config -> default");

    // Key in the wrong section is not the launch key.
    const std::string wrongSection = WriteTempIni("sui_launch_wrong.ini", "[network]\nmode = headless\n");
    Check(Resolve({}, wrongSection.c_str()) == DefaultMode(), "mode outside [launch] is ignored");

    for (const char* name : {"sui_launch_sectioned.ini", "sui_launch_flat.ini", "sui_launch_padded.ini",
             "sui_launch_noisy.ini", "sui_launch_unknown.ini", "sui_launch_empty.ini", "sui_launch_wrong.ini"})
    {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::temp_directory_path() / name, ec);
    }
}

void TestHelpers()
{
    LaunchMode parsed = LaunchMode::Cli;
    Check(serverui::ParseLaunchMode("gui", &parsed) && parsed == LaunchMode::Gui, "ParseLaunchMode(gui)");
    Check(serverui::ParseLaunchMode("headless", &parsed) && parsed == LaunchMode::Headless, "ParseLaunchMode(headless)");
    Check(serverui::ParseLaunchMode("cli", &parsed) && parsed == LaunchMode::Cli, "ParseLaunchMode(cli)");
    Check(serverui::ParseLaunchMode("console", &parsed) && parsed == LaunchMode::Cli, "ParseLaunchMode(console)");
    Check(serverui::ParseLaunchMode("generate-config", &parsed) && parsed == LaunchMode::GenerateConfig,
        "ParseLaunchMode(generate-config)");
    Check(!serverui::ParseLaunchMode("nope", &parsed), "ParseLaunchMode rejects garbage");
    Check(!serverui::ParseLaunchMode(nullptr, &parsed), "ParseLaunchMode rejects null");
    Check(!serverui::ParseLaunchMode("gui", nullptr), "ParseLaunchMode rejects null out");

    Check(std::string(serverui::ToString(LaunchMode::Cli)) == "cli", "ToString(Cli)");
    Check(std::string(serverui::ToString(LaunchMode::Gui)) == "gui", "ToString(Gui)");
    Check(std::string(serverui::ToString(LaunchMode::Headless)) == "headless", "ToString(Headless)");
    Check(std::string(serverui::ToString(LaunchMode::GenerateConfig)) == "generate-config", "ToString(GenerateConfig)");

    // The probe must be callable repeatedly and must agree with itself.
    const bool first = serverui::IsGraphicalEnvironment();
    const bool second = serverui::IsGraphicalEnvironment();
    Check(first == second, "IsGraphicalEnvironment is stable within a run");

#ifdef _WIN32
    std::printf("note: IsGraphicalEnvironment() = %s on this Windows host\n", first ? "true" : "false");
#else
    Check(!first, "IsGraphicalEnvironment is false without a display server");
#endif
}
} // namespace

int main()
{
    TestDefaultFollowsDisplay();
    TestArgumentTable();
    TestConfigFile();
    TestHelpers();

    if (g_failures != 0)
    {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }

    std::printf("launch_mode_test: all checks passed\n");
    return 0;
}
