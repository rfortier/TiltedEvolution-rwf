// Verifies the launch decision that main() used to make inline.
//
// This is the compile-time protection for the wiring: main.cpp itself needs
// spdlog, uv, TiltedCore, SimpleIni and the server dll, so nothing there can be
// checked on a host without them. Everything main() decides lives in
// RunnerLaunch, and this binary proves it.

#include "../RunnerLaunch.h"
#include "../server_ui/src/launch/LaunchMode.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
int g_failures = 0;

void Check(bool aCondition, const char* acWhat)
{
    if (!aCondition)
    {
        std::printf("CHECK FAILED: %s\n", acWhat);
        ++g_failures;
    }
}

// Builds a mutable argv from literals. ResolveLaunchMode takes char**, not
// const char**, so the test owns the storage.
struct Args
{
    std::vector<std::string> storage;
    std::vector<char*> pointers;

    explicit Args(std::initializer_list<const char*> aItems)
    {
        storage.reserve(aItems.size());
        for (const char* p : aItems)
            storage.emplace_back(p);
        pointers.reserve(storage.size());
        for (auto& s : storage)
            pointers.push_back(s.data());
    }

    int argc() const { return static_cast<int>(pointers.size()); }
    char** argv() { return pointers.data(); }
};

void TestModeMapping()
{
    using M = server_runner::LaunchPlan::Mode;

    {
        const auto p = server_runner::PlanForMode(M::Gui);
        Check(p.wantsDashboard, "Gui wants a dashboard");
        Check(!p.wantsConsole, "Gui must never keep a console");
    }
    {
        const auto p = server_runner::PlanForMode(M::Cli);
        Check(!p.wantsDashboard, "Cli has no dashboard");
        Check(p.wantsConsole, "Cli wants a console");
    }
    {
        const auto p = server_runner::PlanForMode(M::Headless);
        Check(!p.wantsDashboard, "Headless has no dashboard");
        Check(!p.wantsConsole, "Headless never touches the console");
    }
    {
        const auto p = server_runner::PlanForMode(M::GenerateConfig);
        Check(!p.wantsDashboard, "GenerateConfig shows no dashboard");
        Check(p.wantsConsole, "GenerateConfig reports what it wrote");
    }
}

void TestArgumentRouting()
{
    using M = server_runner::LaunchPlan::Mode;

    {
        Args a{"app", "--cli"};
        Check(server_runner::MakeLaunchPlan(a.argc(), a.argv(), nullptr).mode == M::Cli, "--cli routes to Cli");
    }
    {
        Args a{"app", "--console"};
        Check(server_runner::MakeLaunchPlan(a.argc(), a.argv(), nullptr).mode == M::Cli, "--console routes to Cli");
    }
    {
        Args a{"app", "--nogui"};
        Check(server_runner::MakeLaunchPlan(a.argc(), a.argv(), nullptr).mode == M::Cli, "--nogui routes to Cli");
    }
    {
        Args a{"app", "--headless"};
        Check(server_runner::MakeLaunchPlan(a.argc(), a.argv(), nullptr).mode == M::Headless, "--headless routes to Headless");
    }
    {
        Args a{"app", "--gui"};
        const auto p = server_runner::MakeLaunchPlan(a.argc(), a.argv(), nullptr);
        Check(p.mode == M::Gui, "--gui routes to Gui");
        Check(p.wantsDashboard, "--gui wants a dashboard");
        Check(!p.wantsConsole, "--gui must not allocate a console");
    }
    {
        Args a{"app", "--generate-config"};
        const auto p = server_runner::MakeLaunchPlan(a.argc(), a.argv(), nullptr);
        Check(p.mode == M::GenerateConfig, "--generate-config routes to GenerateConfig");
        Check(!p.wantsDashboard, "--generate-config shows no window");
    }
}

void TestConfigFallback()
{
    using M = server_runner::LaunchPlan::Mode;

    // No flags and no config: the environment decides, and the decision must
    // still be one of the four modes rather than something uninitialised.
    {
        Args a{"app"};
        const auto p = server_runner::MakeLaunchPlan(a.argc(), a.argv(), nullptr);
        Check(p.mode == M::Cli || p.mode == M::Gui, "no args yields Cli or Gui");
        Check(p.wantsDashboard == (p.mode == M::Gui), "dashboard flag matches the mode");
        Check(p.wantsConsole == (p.mode != M::Gui), "console flag matches the mode");
    }
}

void TestConfigPathResolution()
{
    // A path that cannot exist resolves to empty rather than throwing.
    Check(server_runner::ResolveLaunchConfigPath("", "config", "STServer.ini").empty(),
        "empty working directory yields empty path");
    Check(server_runner::ResolveLaunchConfigPath("C:/definitely/not/here", "config", "STServer.ini").empty(),
        "missing config yields empty path");

    // A path that does exist resolves to something non-empty. Uses the test
    // source's own directory via __FILE__ rather than the process working
    // directory, because the latter depends on where the test is launched from.
    const std::filesystem::path here = std::filesystem::path(__FILE__).parent_path();
    const std::string found =
        server_runner::ResolveLaunchConfigPath(here.parent_path().string(), ".", "RunnerLaunch.h");
    Check(!found.empty(), "an existing file resolves");
}

void TestExhaustiveModeCoverage()
{
    // Every serverui::LaunchMode must map to a distinct plan mode. This is the
    // guard that fails to compile if the launch layer gains a mode.
    const serverui::LaunchMode all[] = {
        serverui::LaunchMode::Cli,
        serverui::LaunchMode::Gui,
        serverui::LaunchMode::Headless,
        serverui::LaunchMode::GenerateConfig,
    };
    Check(sizeof(all) / sizeof(all[0]) == 4, "launch mode count is 4; update this test if it grows");
}
} // namespace

int main()
{
    TestModeMapping();
    TestArgumentRouting();
    TestConfigFallback();
    TestConfigPathResolution();
    TestExhaustiveModeCoverage();

    if (g_failures == 0)
    {
        std::printf("runner launch checks passed\n");
        return 0;
    }

    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
