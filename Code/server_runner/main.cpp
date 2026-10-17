
#include <TiltedCore/Filesystem.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Setting.h>
#include <base/simpleini/SimpleIni.h>
#include <base/threading/ThreadUtils.h>

#include "DediRunner.h"

#ifdef _WIN32
#include "gui/ServerGui.h"
#endif

// The dual-mode launch layer. Kept behind a macro so a build without server_ui
// still produces the classic console runner.
#if defined(SERVERUI_ENABLED)
#include "../server_ui/src/launch/ConsoleHost.h"
#include "../server_ui/src/launch/LaunchMode.h"
#include "../server_ui/src/core/ServerCore.h"
#include "DediCoreBackend.h"
#include "RunnerLaunch.h"

// The ImGui dashboard host needs the imgui package. Where it is not available
// the runner falls back to the dependency-free Win32 panel, which is what the
// original control panel already was.
#if defined(SERVERUI_DASHBOARD_IMGUI)
#if defined(_WIN32)
#include "DashboardHost.h"
#else
#include "DashboardHostLinux.h"
#endif
#include "../server_ui/src/ui/DashboardModel.h"
#include "../server_ui/src/log/LogRing.h"
#include "../server_ui/src/log/LogModel.h"
#include "../server_ui/src/log/LogSink.h"
#include <spdlog/spdlog.h>
#endif
#endif

#ifdef _WIN32
#include <base/dialogues/win/TaskDialog.h>
#pragma comment(lib, "Comctl32.lib")
#elif defined(__linux__)
#include <signal.h>
#endif

namespace
{
constexpr char kLogFileName[] = "STServerOut.log";
// Its fine for us if several potential server instances read this, since its a tilted platform thing
// and therefore not considered game specific.
constexpr char kEULAName[] = "EULA.txt";
constexpr char kEULAText[] = ";Please indicate your agreement to the Tilted platform service agreement\n"
                             ";by setting bConfirmEULA to true\n"
                             "[EULA]\n";
constexpr char kEULATextFalse[] = "bConfirmEULA=false";
constexpr char kEULATextTrue[] = "bConfirmEULA=true";

namespace fs = std::filesystem;

Console::StringSetting sLogLevel{"sLogLevel", "How much detail the server log shows: trace, debug, info, warn or error", "info"};
using namespace std::chrono_literals;
} // namespace

extern Console::Setting<bool> bConsole;

GS_IMPORT void SetDefaultLogger(std::shared_ptr<spdlog::logger> aLogger);
GS_IMPORT void RegisterLogger(std::shared_ptr<spdlog::logger> aLogger);

struct LogInstance
{
    static constexpr size_t kLogFileSizeCap = 1048576 * 5;

    LogInstance()
    {
        using namespace spdlog;

        std::error_code ec;
        fs::create_directory("logs", ec);

        auto consoleOut = spdlog::stdout_color_mt(KCompilerStopThisBullshit);
        consoleOut->set_pattern(">%$ %v");

        // make the client aware of this logger.
        RegisterLogger(consoleOut);

        auto fileOut = std::make_shared<sinks::rotating_file_sink_mt>(std::string("logs/") + kLogFileName, kLogFileSizeCap, 3);
        auto serverOut = std::make_shared<sinks::stdout_color_sink_mt>();
        auto globalOut = std::make_shared<logger>("", sinks_init_list{serverOut, fileOut});
        globalOut->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] [tid %t] %$ %v");

        globalOut->set_level(level::from_str(sLogLevel.value()));
        spdlog::flush_every(std::chrono::seconds(2));

        // as the library is compiled into the client + server we have to do this twice
        spdlog::set_default_logger(globalOut);

        // also make the client aware of the file loggers
        SetDefaultLogger(globalOut);
    }

    ~LogInstance() { spdlog::shutdown(); }
};

static bool RegisterQuitHandler()
{
#if defined(_WIN32)
    static auto CtrlHandler = ([](DWORD aType) {
        switch (aType)
        {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT: {
            if (auto* pRunner = GetDediRunner())
            {
                pRunner->RequestKill();
                return TRUE;
            }
            // if the user kills during the ctor we deny the request
            // to save our dear life.
            return FALSE;
        }
        default:
            return FALSE;
        }
    });

    return SetConsoleCtrlHandler(CtrlHandler, TRUE);

#elif defined(__linux__)
    static auto CtrlHandler = ([](int aSig) {
        if (auto* pRunner = GetDediRunner())
        {
            pRunner->RequestKill();
        }
    });

    signal(SIGINT, CtrlHandler);
    signal(SIGTERM, CtrlHandler);
    signal(SIGKILL, CtrlHandler);
#else
    return true;
#endif
}

#ifdef _WIN32
static bool ShowEULADialog()
{
    Base::TaskDialog dia(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102)), L"Tilted Platform Agreement", L"Confirm the Tilted Platform EULA", L"TODO: Link to EULA", nullptr);
    dia.AppendButton(100, L"Accept EULA");
    dia.AppendButton(101, L"Deny EULA");
    dia.SetDefaultButton(101 /*So they have to think about it*/);

    return dia.Show() == 100;
}
#endif

// The eula can be accepted in 3 ways:
// - Either confirm the dialog on startup(windows only)
// - Or signal agreement using an environment variable
// - Or Simply set bConfirmEULA in EULA.txt to true
static bool IsEULAAccepted()
{
    const auto path = fs::current_path() / kConfigPathName / kEULAName;

    bool preAccept = false;
    if (char* pValue = std::getenv("TILTED_ACCEPT_EULA"))
    {
        std::string_view env(pValue);
        preAccept = env == "true" || env == "1" || env == "TRUE";
    }

    auto saveFile = [&]()
    {
#ifdef _WIN32
        // try using the dialog
        if (!preAccept)
            preAccept = ShowEULADialog();
#endif
        fs::create_directory(fs::current_path() / kConfigPathName);

        TiltedPhoques::String eulaText = kEULAText;
        eulaText += preAccept ? kEULATextTrue : kEULATextFalse;

        TiltedPhoques::SaveFile(path, eulaText);
        return preAccept;
    };

    if (!exists(path))
    {
        return saveFile();
    }

    const auto data = TiltedPhoques::LoadFile(path);

    CSimpleIni si;

    if (si.LoadData(data.c_str()) != SI_OK)
        return preAccept;

    if (!si.GetBoolValue("EULA", "bConfirmEULA", false))
    {
#ifdef _WIN32
        preAccept = false;
        return saveFile();
#else
        return false;
#endif
    }

    return true;
}

GS_IMPORT bool CheckBuildTag(const char* apBuildTag);

void ConfigureConsoleMode()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    // Set the input code page to UTF-8
    SetConsoleCP(CP_UTF8);
#endif
}

#if defined(SERVERUI_ENABLED)
// The launch decision lives in RunnerLaunch.cpp so it can be compiled and
// tested without spdlog/uv/TiltedCore/SimpleIni, none of which are available
// on every host. main() only applies the plan here.
static server_runner::LaunchPlan MakePlan(int argc, char** argv)
{
    const std::string configPath = server_runner::ResolveLaunchConfigPath(
        fs::current_path().string(), kConfigPathName, "STServer.ini");
    return server_runner::MakeLaunchPlan(argc, argv, configPath.empty() ? nullptr : configPath.c_str());
}
#endif

int main(int argc, char** argv)
{
    ConfigureConsoleMode();

    // the binaries are not from the same commit.
    if (!CheckBuildTag(kBuildTag))
        return 1;

    Base::SetCurrentThreadName("ServerRunnerMain");

#if defined(SERVERUI_ENABLED)
    // Decide before anything heavy is constructed, so a GUI run never allocates
    // a console. The decision itself is compiled and tested in RunnerLaunch.cpp.
    const server_runner::LaunchPlan plan = MakePlan(argc, argv);

    // CLI attaches to the parent console (or allocates one) and rebinds
    // stdout/stderr; GUI drops any console the process was handed, which is
    // what removes the black window.
    if (plan.wantsConsole)
        serverui::EnsureConsoleForMode(serverui::LaunchMode::Cli);
    else
        serverui::EnsureConsoleForMode(serverui::LaunchMode::Gui);

#if defined(SERVERUI_DASHBOARD_IMGUI)
    // Declared BEFORE the logger, and therefore destroyed AFTER it.
    //
    // Destruction is the reverse of declaration: a ring declared here outlives
    // LogInstance, whose destructor calls spdlog::shutdown(). That matters
    // because spdlog::flush_every() runs a background flush thread, and the sink
    // this ring backs is registered on two loggers. If the ring went first, a
    // flush that landed in the window between ring destruction and
    // spdlog::shutdown() would push into freed memory.
    //
    // 1 MiB: the ring drops rather than blocking, so this trades a bounded
    // amount of memory for a guaranteed-non-blocking producer.
    serverui::LogRing logRing(1u << 20);
#endif

    LogInstance logger;
    (void)logger;

    if (plan.wantsDashboard)
    {
      // Nested scope: the core, the log model and the dashboard state are all
      // destroyed before main() can reach the classic console path below. There
      // is no automatic downgrade any more, so in practice this block either
      // returns or exits - but keeping the lifetime explicit means a future
      // change cannot accidentally leave two servers alive at once.
      {
        auto pRunner = std::make_unique<DediRunner>(argc, argv);
        if (!pRunner->IsValid())
        {
            // --generate-config and settings failures land here; there is no
            // server to show.
            serverui::ReleaseConsole();
            return 0;
        }

        serverui::CoreConfig config;
        config.iniPath = (fs::current_path() / kConfigPathName / "STServer.ini").string();

        auto pBackend = std::make_unique<server_runner::DediCoreBackend>(std::move(pRunner));
        serverui::ServerCore core(config, std::move(pBackend));

        if (!core.Start())
        {
            serverui::ReleaseConsole();
            return 1;
        }

#if defined(SERVERUI_DASHBOARD_IMGUI)
        // The ImGui dashboard: ring (already declared above, so it outlives the
        // logger) -> sink on both loggers -> model -> draw.
        serverui::LogModel logModel(100000);

        // Two loggers carry server output and they are NOT the same logger:
        // ConOut holds every console-command response, the default logger holds
        // game/server lines. Attaching to only one silently loses half of it.
        // A sink instance is shared by both loggers; LogSink forwards into the
        // ring, which is thread safe by construction.
        auto pLogSink = std::make_shared<serverui::LogSink>(logRing);
        auto attachSink = [&pLogSink](const std::shared_ptr<spdlog::logger>& apLogger)
        {
            if (apLogger)
                apLogger->sinks().push_back(pLogSink);
        };
        attachSink(spdlog::get(KCompilerStopThisBullshit));
        attachSink(spdlog::default_logger());

        serverui::DashboardModel model(logModel);

        // Refreshes the model on the UI thread, once per frame, immediately
        // before it is read by the draw code.
        //
        // This used to run on a background thread with its own sleep loop; that
        // was a data race. DashboardModel holds a std::vector<PlayerRow>, a
        // Sparkline history and the scroll state, none of it synchronised, so a
        // producer thread writing it while the renderer read it is undefined
        // behaviour - and the visible symptom would be torn rows or a corrupted
        // history rather than anything obviously wrong.
        //
        // Doing it per frame is also cheaper than it looks: Snapshot() is a
        // lock-free atomic read, Players() is a small vector copy, and the ring
        // drain is bounded. At 60 fps this is the natural cadence anyway.
        auto beforeFrame = [&core, &logRing, &logModel, &model](serverui::DashboardModel& aModel)
        {
            // The drain is bounded per call (at most SlotCount()), so loop until
            // it reports empty. One call does not empty a large ring.
            static thread_local std::vector<serverui::LogRecord> records(4096);
            for (;;)
            {
                const size_t n = logRing.Drain(records.data(), records.size());
                if (n == 0)
                    break;
                logModel.AppendBatch(records.data(), n);
            }

            const uint64_t nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            aModel.Update(core.Snapshot(), nowMs);
            aModel.SetPlayers(core.Players());
        };

        server_runner::DashboardHostActions hostActions;
        hostActions.userData = &core;
        hostActions.gracefulShutdown = [](void* apUser) { static_cast<serverui::ServerCore*>(apUser)->RequestStop(); };
        hostActions.submitCommand = [](void* apUser, const char* acLine)
        {
            if (acLine != nullptr)
                static_cast<serverui::ServerCore*>(apUser)->SubmitCommand(acLine);
        };
        // The host owns the model, so the hook receives it rather than capturing
        // one: a captured reference could outlive the model it points at.
        hostActions.beforeFrame = [&beforeFrame](serverui::DashboardModel& aModel)
        {
            beforeFrame(aModel);
        };

#if defined(_WIN32)
        const int result = server_runner::RunDashboardWindow(
            model, hostActions, L"Skyrim Together Next - Dedicated Server", 1280, 800);
#else
        // The Linux host takes a layout-identical actions struct declared in its
        // own header, so including that header never drags D3D11 into a Linux
        // build. Copy field by field rather than reinterpreting one as the other.
        server_runner::DashboardHostActionsLinux linuxActions;
        linuxActions.userData = hostActions.userData;
        linuxActions.gracefulShutdown = hostActions.gracefulShutdown;
        linuxActions.submitCommand = hostActions.submitCommand;
        linuxActions.beforeFrame = hostActions.beforeFrame;

        const int result = server_runner::RunDashboardWindowLinux(
            model, linuxActions, "Skyrim Together Next - Dedicated Server", 1280, 800);
#endif

        core.RequestStop();

        // No automatic downgrade. If the window could not be opened, the mode
        // the user (or the environment probe) selected is simply unavailable, and
        // silently continuing as a console server would hide that: a deployment
        // monitoring for a GUI would look healthy while running something else.
        // Report the failure and exit non-zero; --cli and --headless exist for
        // running without a window on purpose.
        if (result == server_runner::kDashboardWindowUnavailable)
        {
            spdlog::error(
                "The dashboard window could not be opened. The server was NOT started. "
                "Use --cli or --headless to run without a window.");
        }

        serverui::ReleaseConsole();
        return result;
#elif defined(_WIN32)
        // No imgui package: fall back to the dependency-free Win32 panel rather
        // than failing to start. Same core, same single server.
        const int result = server_gui::RunGuiWithCore(argc, argv, core);

        core.RequestStop();
        serverui::ReleaseConsole();
        return result;
#else
        // No GUI on this platform. ResolveLaunchMode can only return Gui here if
        // something forced it (a hand-written config key, say), and there is no
        // panel to link against. Report it rather than pretending to start.
        core.RequestStop();
        serverui::ReleaseConsole();
        return 1;
#endif
      } // scope: core, model and dashboard state end here
    }

    // Cli and Headless share the classic path below. Headless never attached a
    // console, so its output goes to the file sink only.
    if (plan.wantsConsole && bConsole)
        ConfigureConsoleMode();
#else
    LogInstance logger;
    (void)logger;

#ifdef _WIN32
    // windowed control panel unless explicitly disabled; it runs the same
    // DediRunner loop on a background thread
    if (server_gui::RequestGuiMode(argc, argv))
        return server_gui::RunGui(argc, argv);
#endif
#endif // SERVERUI_ENABLED

    // Note(Vince): This started crashing on 1.7+ lets disable it for now.
    // RegisterQuitHandler();

    // Keep stack free.
    const auto cpRunner{std::make_unique<DediRunner>(argc, argv)};
    if (!cpRunner->IsValid())
        return 0;

    if (bConsole)
    {
        cpRunner->StartTerminalIO();
    }

    cpRunner->RunGSThread();

    return 0;
}
