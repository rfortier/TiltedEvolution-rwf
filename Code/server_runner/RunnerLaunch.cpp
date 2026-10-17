#include "RunnerLaunch.h"

#include "../server_ui/src/launch/LaunchMode.h"

#include <filesystem>

namespace server_runner
{

namespace
{
// Keeps the two enumerations in lockstep. If serverui::LaunchMode gains a mode,
// this stops compiling instead of silently defaulting it to Cli.
constexpr LaunchPlan::Mode ToPlanMode(serverui::LaunchMode aMode) noexcept
{
    switch (aMode)
    {
    case serverui::LaunchMode::Cli: return LaunchPlan::Mode::Cli;
    case serverui::LaunchMode::Gui: return LaunchPlan::Mode::Gui;
    case serverui::LaunchMode::Headless: return LaunchPlan::Mode::Headless;
    case serverui::LaunchMode::GenerateConfig: return LaunchPlan::Mode::GenerateConfig;
    }
    return LaunchPlan::Mode::Cli;
}
} // namespace

std::string ResolveLaunchConfigPath(std::string_view aWorkingDirectory, std::string_view aConfigDir,
    std::string_view aConfigFile)
{
    if (aWorkingDirectory.empty() || aConfigDir.empty() || aConfigFile.empty())
        return {};

    std::filesystem::path path(aWorkingDirectory);
    path /= aConfigDir;
    path /= aConfigFile;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return {};

    return path.string();
}

LaunchPlan PlanForMode(LaunchPlan::Mode aMode)
{
    LaunchPlan plan;
    plan.mode = aMode;

    switch (aMode)
    {
    case LaunchPlan::Mode::Gui:
        plan.wantsDashboard = true;
        plan.wantsConsole = false; // a GUI launch must never keep a console
        plan.constructRunner = true;
        break;

    case LaunchPlan::Mode::Cli:
        plan.wantsDashboard = false;
        plan.wantsConsole = true;
        plan.constructRunner = true;
        break;

    case LaunchPlan::Mode::Headless:
        plan.wantsDashboard = false;
        plan.wantsConsole = false; // file logging only
        plan.constructRunner = true;
        break;

    case LaunchPlan::Mode::GenerateConfig:
        plan.wantsDashboard = false;
        plan.wantsConsole = true;
        // The runner's own settings path handles the write, so the object is
        // still constructed, but no dashboard is ever shown for it.
        plan.constructRunner = true;
        break;
    }

    return plan;
}

LaunchPlan MakeLaunchPlan(int argc, char** argv, const char* acConfigPath)
{
    const serverui::LaunchMode mode = serverui::ResolveLaunchMode(argc, argv, acConfigPath);
    return PlanForMode(ToPlanMode(mode));
}

} // namespace server_runner
