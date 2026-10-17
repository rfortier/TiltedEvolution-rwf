#pragma once

// RunnerLaunch: the launch decision, extracted from main() so it can be
// compiled and tested without spdlog, uv, TiltedCore, SimpleIni or the server
// dll.
//
// Why this exists
// ---------------
// main.cpp is the one file in the wiring that cannot be compiled on a host
// without the whole dependency stack, which made it the weakest link: a typo
// there is invisible until someone with xmake builds it. Everything main() has
// to *decide* lives here instead, where a stub build can check it.
//
// This header must stay free of project dependencies. It may use std:: only.
// It deliberately does not include DediRunner.h, ServerCore.h or spdlog.

#include <cstdint>
#include <string>
#include <string_view>

namespace serverui
{
enum class LaunchMode; // defined in launch/LaunchMode.h
}

namespace server_runner
{

// What main() should do, decided from the arguments, the config file and the
// environment, but with every side effect left to the caller.
struct LaunchPlan
{
    // Mirrors serverui::LaunchMode as a plain enum so this header does not drag
    // in the launch layer. Keep in sync with it; there is a static_assert in the
    // .cpp that fails the build if the two ever disagree.
    enum class Mode
    {
        Cli,
        Gui,
        Headless,
        GenerateConfig,
    };

    Mode mode{Mode::Cli};

    // True when the process should open the dashboard window. Only ever true
    // for Mode::Gui on a platform that has one.
    bool wantsDashboard{false};

    // True when a console must be attached or allocated (Cli / GenerateConfig).
    bool wantsConsole{false};

    // True when the runner object should be constructed at all. False for
    // --generate-config handled entirely by the runner's own settings path and
    // for an invalid command line.
    bool constructRunner{true};
};

// Resolves the config file whose launch/mode key participates in the decision.
// Returns an empty string when it does not exist, which disables that step.
std::string ResolveLaunchConfigPath(std::string_view aWorkingDirectory, std::string_view aConfigDir,
    std::string_view aConfigFile);

// The decision itself. acConfigPath may be null/empty.
//
// Precedence is owned by serverui::ResolveLaunchMode; this function only maps
// its answer onto a LaunchPlan plus the two flags the caller needs, and is kept
// separate so the mapping is testable without linking the launch layer.
LaunchPlan MakeLaunchPlan(int argc, char** argv, const char* acConfigPath);

// Maps a launch mode onto a plan without touching the environment. Exposed for
// the test; MakeLaunchPlan calls it.
LaunchPlan PlanForMode(LaunchPlan::Mode aMode);

} // namespace server_runner
