
#include "DediRunner.h"

#include <console/CommandSettingsProvider.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <base/threading/ThreadUtils.h>

namespace
{
constexpr char kSettingsFileName[] = "STServer.ini";
constexpr char kGenerateConfigArg[] = "--generate-config";

// Upper bound on console commands produced between two console drains.
//
// Console::CommandQueue is a fast_queue<Item, 2>, i.e. four slots, and its
// write_acquire() busy-waits until the slot it grabbed has been released; the
// only reader is ConsoleRegistry::Update(), which does not run until the tick
// is over. Pasting N lines arrives as one libuv read callback, so producing all
// N lines there parks the writer forever on line five and freezes the server.
// The bound is deliberately below the slot count and is not consumed by a
// single callback either: how often libuv re-enters a read callback inside one
// uv_run is not something this code can control, so the budget belongs to the
// tick. Surplus lines stay buffered for the next tick rather than being
// dropped, which is why the ceiling costs nothing at the keyboard.
constexpr size_t kMaxCommandsPerTick = 2;

// Ceiling on the bytes held in DediRunner::m_stdinPending.
//
// ReadStdin() appends whatever libuv hands it in one callback, and a single
// read of a pasted script or of a pipe being fed by another process can be
// arbitrarily large, while a tick only ever hands kMaxCommandsPerTick lines to
// the console. Nothing between the two paths caps the amount of unparsed input
// the runner can be made to hold, so a paste of a few hundred megabytes - or a
// peer that keeps writing - grows the buffer until the process dies. 64 KiB is
// two orders of magnitude above the longest line any console command needs, so
// it never truncates a legitimate paste, and it is small enough that the worst
// case the buffer can reach is not a memory figure worth reasoning about.
//
// The overflow response is "drop the bytes and say so", not "keep growing" and
// not "stop reading". Growing is the bug. Blocking is not available either: the
// read callback runs on the tick thread, the bytes have already been taken out
// of the terminal by the time this runs, and pausing the handle would only push
// the unread part back into libuv's own buffering - the input is accepted with
// no ceiling and no diagnosis, and the server stalls waiting for a caller that
// may never press enter. What remains inside this callback is to drop and
// report. It is dropped rather than truncated for the same reason it is not
// kept: the buffer at that point holds an unterminated prefix of one command,
// and preserving it would splice that tail onto the next command the user types
// - silent corruption of unrelated input instead of a refusal the user sees.
constexpr size_t kMaxPendingStdinBytes = 64 * 1024;

DediRunner* s_pRunner{nullptr};
} // namespace

// imports
GS_IMPORT void RegisterServerSettings(Console::ConsoleRegistry& conReg);
GS_IMPORT TiltedPhoques::UniquePtr<IGameServerInstance> CreateGameServer(Console::ConsoleRegistry& conReg);
// needs to be global
Console::Setting bConsole{"bConsole", "Enable the console", true};

DediRunner* GetDediRunner() noexcept
{
    return s_pRunner;
}

DediRunner::DediRunner(int argc, char** argv)
    : m_console(KCompilerStopThisBullshit)
{
    s_pRunner = this;

    uv_loop_init(&m_loop);

    // settings must be registered before they can be loaded
    RegisterServerSettings(m_console);
    if (!LoadSettings(argc, argv))
        return;

    m_pServerInstance = CreateGameServer(m_console);

    // it is here for now..
    m_pServerInstance->Initialize();
    SaveSettingsToIni(m_console, m_SettingsPath);
}

DediRunner::~DediRunner()
{
    if (m_useIni)
        SaveSettingsToIni(m_console, m_SettingsPath);
    uv_loop_close(&m_loop);
}

bool DediRunner::LoadSettings(int argc, char** argv)
{
    // --generate-config [path]: write the default settings and don't start the server
    if (argc > 1 && std::strcmp(argv[1], kGenerateConfigArg) == 0)
    {
        const fs::path path = argc > 2 ? fs::path(argv[2]) : fs::current_path() / kConfigPathName / kSettingsFileName;
        if (path.has_parent_path())
            create_directories(path.parent_path());
        SaveSettingsToIni(m_console, path);
        spdlog::info("Generated {}", path.string());
        return false;
    }

    // If we have line args load, don't use the ini
    if (argc > 1)
    {
        LoadSettingsFromCommand(m_console, argc, argv);
    }
    else
    {
        m_useIni = true;
        m_SettingsPath = fs::current_path() / kConfigPathName / kSettingsFileName;
        if (!exists(m_SettingsPath))
        {
            create_directory(fs::current_path() / kConfigPathName);
            SaveSettingsToIni(m_console, m_SettingsPath);
            return true;
        }
        LoadSettingsFromIni(m_console, m_SettingsPath);
    }

    return true;
}

// Payload behind m_tty.data: libuv hands back a void*, and the read callback
// needs the runner to reach its pending-byte buffer.
struct Context
{
    DediRunner* instance;
};

void DediRunner::ReadStdin(uv_stream_t* apStream, ssize_t aRead, const uv_buf_t* acpBuffer)
{
    if (aRead < 0)
    {
        if (aRead == UV_EOF)
            uv_close(reinterpret_cast<uv_handle_t*>(&apStream), nullptr);
    }
    else if (aRead > 0)
    {
        // The bytes are buffered on the runner, not on the read context: this
        // callback is not the only path that has to consume them.
        auto* ctx = static_cast<Context*>(apStream->data);
        auto* pInstance = ctx->instance;
        const auto read = static_cast<size_t>(aRead);

        // Refuse the append that would take the buffer past its ceiling (see
        // kMaxPendingStdinBytes for why this drops instead of blocking). The
        // buffer is cleared with the refusal because what sits in it is an
        // unterminated prefix; appending the next read to that prefix would make
        // the following command out of two unrelated halves. Errors, not warns:
        // every byte counted here is input the user typed and the server did not
        // run, and the count is what tells them how much has to be typed again.
        //
        // This is an else and not an early return on purpose: the free below is
        // the only owner of the read buffer libuv handed over, so leaving the
        // function here would leak that allocation on every refused read - an
        // unbounded leak standing in for the unbounded buffer it replaces.
        if (pInstance->m_stdinPending.size() + read > kMaxPendingStdinBytes)
        {
            spdlog::error("Stdin pending buffer exceeded {} bytes, dropping {} byte(s) read and clearing the buffer",
                          kMaxPendingStdinBytes, read);
            // Assigned rather than clear()ed: this exact member was reset with an
            // empty literal before the buffering was introduced, so it is the one
            // reset the tree proves TiltedPhoques::String supports.
            pInstance->m_stdinPending = "";
        }
        else
        {
            pInstance->m_stdinPending.append(acpBuffer->base, read);
            pInstance->DrainStdinBuffer(pInstance->m_stdinPending);
        }
    }

    // OK to free buffer as write_data copies it.
    if (acpBuffer->base)
        TiltedPhoques::Allocator::GetDefault()->Free(acpBuffer->base);
}

void DediRunner::DrainStdinBuffer(TiltedPhoques::String& aBuffer)
{
    size_t consumed = 0;

    while (m_stdinBudget != 0)
    {
        const size_t newline = aBuffer.find('\n', consumed);
        if (newline == TiltedPhoques::String::npos)
            break;

        // Running a line uploads into the console queue immediately, so the
        // budget has to be spent here and not measured afterwards.
        HandleConsole(aBuffer.substr(consumed, newline - consumed));
        m_stdinBudget--;
        consumed = newline + 1;
    }

    if (consumed != 0)
        aBuffer.erase(0, consumed);
}

void DediRunner::PumpPendingCommands()
{
    // Complete lines can be left buffered by a read callback that ran out of
    // budget. Nothing would ever look at them again: libuv only repeats a read
    // callback when new bytes arrive, so a paste that ends the burst would
    // strand the tail of the paste forever. This runs on the same thread right
    // after uv_run, before the console drains, so it cannot race with a read.
    if (!m_stdinPending.empty())
        DrainStdinBuffer(m_stdinPending);
}

void DediRunner::AllocateBuffer(uv_handle_t* apHandle, size_t aSuggestedSize, uv_buf_t* apBuffer)
{
    *apBuffer = uv_buf_init(static_cast<char*>(TiltedPhoques::Allocator::GetDefault()->Allocate(aSuggestedSize)), static_cast<uint32_t>(aSuggestedSize));
}

void DediRunner::PrintExecutorArrowHack()
{
    // Force:
    // This is a hack to get the executor arrow.
    // If you find a way to do this through the ConOut log channel
    // please let me know (The issue is the forced formatting for that channel and the forced null termination)
    // fmt::print(">>>");
}

bool DediRunner::TickOnce()
{
    if (m_pServerInstance == nullptr || !m_pServerInstance->IsListening())
        return false;

    m_pServerInstance->Update();

    if (bConsole)
    {
        // Everything uv_run produces is uploaded before the console drains
        // below, so the budget is renewed here and nowhere else.
        m_stdinBudget = kMaxCommandsPerTick;
        uv_run(&m_loop, UV_RUN_NOWAIT);
        PumpPendingCommands();
        if (m_console.Update())
            PrintExecutorArrowHack();
    }

    return m_pServerInstance->IsListening();
}

void DediRunner::RunGSThread()
{
    while (TickOnce())
    {
    }
}

void DediRunner::StartTerminalIO()
{
    spdlog::get("ConOut")->info("Server started, type /help for a list of commands.");
    PrintExecutorArrowHack();

    uv_tty_init(&m_loop, &m_tty, 0, 1);
    uv_tty_set_mode(&m_tty, UV_TTY_MODE_NORMAL);

    static Context ctx;
    ctx.instance = this;

    m_tty.data = &ctx;

    uv_read_start(reinterpret_cast<uv_stream_t*>(&m_tty), AllocateBuffer, ReadStdin);
}

void DediRunner::RequestKill()
{
    m_pServerInstance->Shutdown();

#if defined(_WIN32)
    // work around Control Handler exception (Control-C) being set
    // https://cdn.discordapp.com/attachments/675107843573022779/941772837339930674/unknown.png
    // being set.
    if (IsDebuggerPresent())
    {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(300ms);
    }
#endif
}

void DediRunner::HandleConsole(const TiltedPhoques::String& acCommand)
{
    using exr = Console::ConsoleRegistry::ExecutionResult;

    exr r = m_console.TryExecuteCommand(acCommand);

    PrintExecutorArrowHack();

    if (r == exr::kDirty && m_useIni)
        SaveSettingsToIni(m_console, m_SettingsPath);
}
