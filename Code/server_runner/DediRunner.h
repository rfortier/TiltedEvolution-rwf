#pragma once

// spdlog
#include <spdlog/spdlog.h>

#include <thread>

#include <BuildInfo.h>
#include <uv.h>
#include <console/ConsoleRegistry.h>
#include <console/IniSettingsProvider.h>
#include <common/GameServerInstance.h>

// Import declarations for the STServer shared library.
//
// Windows needs __declspec(dllimport) so the import thunk is generated; without
// it every call goes through a slower indirect stub. On ELF there is no
// equivalent: a symbol resolved from a shared object is simply an undefined
// reference, and attaching a visibility attribute to a *declaration the consumer
// does not define* is misleading - "default" visibility on an import describes
// nothing the runner controls.
#ifdef _WIN32
#define GS_IMPORT extern __declspec(dllimport)
#else
#define GS_IMPORT extern
#endif

namespace fs = std::filesystem;

static constexpr char kConfigPathName[] = "config";
static constexpr char KCompilerStopThisBullshit[] = "ConOut";
static constexpr char kBuildTag[]{BUILD_BRANCH "@" BUILD_COMMIT};

// Frontend class for the dedicated terminal based server
struct DediRunner
{
    DediRunner(int argc, char** argv);
    ~DediRunner();

    void RunGSThread();

    // One iteration of the loop RunGSThread() runs: advance the server, then
    // service the console. Exposed so an external loop (the dashboard's
    // ServerCore) can drive the same work instead of spawning a second loop.
    // Returns false once the server has stopped listening.
    bool TickOnce();

    void StartTerminalIO();
    void RequestKill();
    void HandleConsole(const TiltedPhoques::String& acCommand);

    // false if the server wasn't created, e.g. with --generate-config
    bool IsValid() const noexcept { return m_pServerInstance != nullptr; }

private:
    static void PrintExecutorArrowHack();

    // returns false if the server shouldn't be started
    bool LoadSettings(int argc, char** argv);

    static void ReadStdin(uv_stream_t* apStream, ssize_t aRead, const uv_buf_t* acpBuffer);
    static void AllocateBuffer(uv_handle_t* apHandle, size_t aSuggestedSize, uv_buf_t* apBuffer);

    // Hands the complete lines of aBuffer to the console while m_stdinBudget
    // lasts; whatever is left stays in aBuffer. Shared by the libuv read
    // callback and the per-tick pump so both draw on the same tick ceiling.
    void DrainStdinBuffer(TiltedPhoques::String& aBuffer);
    // Processes the lines a read callback had to leave buffered, for the case
    // where no further read event ever arrives.
    void PumpPendingCommands();

private:
    // fs::path m_configPath;
    //  Order here matters for constructor calling order.
    uv_loop_t m_loop;
    uv_tty_t m_tty;
    // Partially received console line, plus complete lines a read callback could
    // not hand over without overflowing the command queue. Kept on the runner
    // and not in the terminal's read context because TickOnce() also runs from
    // the dashboard, which never calls StartTerminalIO() at all. Bounded by
    // kMaxPendingStdinBytes in the .cpp: a read that would push it past that
    // ceiling is dropped and reported instead of appended.
    TiltedPhoques::String m_stdinPending;
    // Commands still allowed to be produced before the next console drain.
    size_t m_stdinBudget{0};
    fs::path m_SettingsPath;
    bool m_useIni{false};
    Console::ConsoleRegistry m_console;
    TiltedPhoques::UniquePtr<IGameServerInstance> m_pServerInstance;
};

DediRunner* GetDediRunner() noexcept;
