// Minimal native Win32 control panel for the dedicated server.
// Deliberately dependency free (user32/gdi32 only) so it cannot destabilize
// the server build: the existing console server loop runs on a background
// thread, this window surfaces status, player count, uptime and a log tail,
// plus Start/Stop and Edit-config actions. Passing --nogui (or --console)
// keeps the classic terminal behavior.

#include "ServerGui.h"

#ifdef _WIN32

// this file uses the W variants of the win32 api exclusively; make the
// TCHAR-dependent macros (IDC_ARROW etc.) resolve to the wide versions too
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// winsock2.h must come before Windows.h: DediRunner.h pulls in uv.h and
// the two sockets headers conflict when windows.h got there first
#include <winsock2.h>
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "../DediRunner.h"

// telemetry exports from STServer.dll (see Code/server/main.cpp) - the gui
// must not reach into GameServer.h directly, its include chain drags the
// whole server + TiltedConnect header world along
#ifdef _WIN32
#define GS_GUI_IMPORT extern __declspec(dllimport)
#else
#define GS_GUI_IMPORT extern
#endif
GS_GUI_IMPORT uint32_t QueryServerPlayerCount();
GS_GUI_IMPORT void RequestServerShutdown();

namespace fs = std::filesystem;

namespace
{
constexpr wchar_t kClassName[] = L"STRServerGui";
constexpr wchar_t kWindowTitle[] = L"Skyrim Together Next Server";

constexpr COLORREF kBackground = RGB(24, 26, 32);
constexpr COLORREF kText = RGB(226, 230, 236);

constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 500;
constexpr UINT kRefreshMsg = WM_APP + 1; // posted by the server thread

struct GuiState
{
    HWND window{nullptr};
    HWND statusLabel{nullptr};
    HWND playersLabel{nullptr};
    HWND logView{nullptr};
    HWND startBtn{nullptr};
    HWND stopBtn{nullptr};
    HWND configBtn{nullptr};

    std::thread serverThread;
    std::atomic<bool> serverRunning{false};
    std::atomic<bool> startRequested{false};

    std::chrono::steady_clock::time_point startedAt{};

    std::mutex logMutex;
    std::deque<std::wstring> logLines;

    int storedArgc{0};
    char** storedArgv{nullptr};
};

GuiState* g_gui{nullptr};

void AppendLogLine(const std::wstring& aLine)
{
    if (!g_gui)
        return;

    std::scoped_lock lock(g_gui->logMutex);
    g_gui->logLines.push_back(aLine);
    while (g_gui->logLines.size() > 400)
        g_gui->logLines.pop_front();
}

unsigned QueryPlayerCount()
{
    return QueryServerPlayerCount();
}

void StartServerThread(int argc, char** argv)
{
    if (g_gui->serverRunning.exchange(true))
        return;

    // the runner cannot be constructed twice in one process; a stopped
    // server stays stopped until the app is relaunched
    if (g_gui->startRequested.exchange(true))
    {
        g_gui->serverRunning.store(false);
        return;
    }

    g_gui->startedAt = std::chrono::steady_clock::now();
    g_gui->serverThread = std::thread([argc, argv]() {
        AppendLogLine(L"server: starting");

        auto runner = std::make_unique<DediRunner>(argc, argv);
        AppendLogLine(L"server: running (port and settings are logged to the console file)");
        runner->RunGSThread();
        // runner intentionally leaks its shutdown here; process exit cleans up

        if (g_gui)
        {
            g_gui->serverRunning.store(false);
            AppendLogLine(L"server: stopped");
            PostMessageW(g_gui->window, kRefreshMsg, 0, 0);
        }
    });
}

void StopServerThread()
{
    if (!g_gui->serverRunning.load())
        return;

    AppendLogLine(L"server: stop requested");
    RequestServerShutdown();
}

void FormatUptime(wchar_t* aBuffer, size_t aSize)
{
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - g_gui->startedAt).count();

    const auto h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    swprintf_s(aBuffer, aSize, L"Uptime:  %02dh %02dm %02ds", static_cast<unsigned>(h), static_cast<unsigned>(m), static_cast<unsigned>(s));
}

void UpdateLabels()
{
    if (!g_gui)
        return;

    const bool running = g_gui->serverRunning.load();
    SetWindowTextW(g_gui->statusLabel, running ? L"Status:  Running"
                                               : (g_gui->startRequested.load() ? L"Status:  Stopped (restart the app to run again)"
                                                                               : L"Status:  Stopped"));

    wchar_t players[64]{};
    swprintf_s(players, L"Players:  %u", running ? QueryPlayerCount() : 0u);
    SetWindowTextW(g_gui->playersLabel, players);

    EnableWindow(g_gui->startBtn, !running && !g_gui->startRequested.load());
    EnableWindow(g_gui->stopBtn, running);
}

void PumpLog()
{
    if (!g_gui)
        return;

    std::wstring text;
    {
        std::scoped_lock lock(g_gui->logMutex);
        for (const auto& line : g_gui->logLines)
            text += line + L"\r\n";
    }
    SetWindowTextW(g_gui->logView, text.c_str());

    // keep the tail visible
    const LRESULT lines = SendMessageW(g_gui->logView, EM_GETLINECOUNT, 0, 0);
    SendMessageW(g_gui->logView, EM_LINESCROLL, 0, lines);
}

void OpenConfigInNotepad()
{
    fs::path configPath = fs::current_path() / "config" / "STServer.ini";
    if (!fs::exists(configPath))
    {
        fs::create_directories(configPath.parent_path());
    }

    ShellExecuteW(nullptr, L"open", L"notepad.exe", configPath.c_str(), nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        g_gui = static_cast<GuiState*>(cs->lpCreateParams);

        g_gui->statusLabel = CreateWindowExW(0, L"STATIC", L"Status:  Stopped",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 18, 200, 22, hWnd, nullptr, nullptr, nullptr);
        g_gui->playersLabel = CreateWindowExW(0, L"STATIC", L"Players:  0",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 230, 18, 140, 22, hWnd, nullptr, nullptr, nullptr);

        g_gui->startBtn = CreateWindowExW(0, L"BUTTON", L"Start",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 400, 14, 90, 28, hWnd, reinterpret_cast<HMENU>(1), nullptr, nullptr);
        g_gui->stopBtn = CreateWindowExW(0, L"BUTTON", L"Stop",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 500, 14, 90, 28, hWnd, reinterpret_cast<HMENU>(2), nullptr, nullptr);
        g_gui->configBtn = CreateWindowExW(0, L"BUTTON", L"Edit config",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 600, 14, 120, 28, hWnd, reinterpret_cast<HMENU>(3), nullptr, nullptr);

        g_gui->logView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 20, 60, 700, 380, hWnd, nullptr, nullptr, nullptr);

        HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        for (HWND h : {g_gui->statusLabel, g_gui->playersLabel, g_gui->logView, g_gui->startBtn, g_gui->stopBtn, g_gui->configBtn})
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        break;
    }

    case WM_TIMER:
        UpdateLabels();
        PumpLog();
        return 0;

    case kRefreshMsg:
        UpdateLabels();
        PumpLog();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 1: StartServerThread(g_gui->storedArgc, g_gui->storedArgv); break;
        case 2: StopServerThread(); break;
        case 3: OpenConfigInNotepad(); break;
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kBackground);
        return reinterpret_cast<LRESULT>(GetStockObject(DKGRAY_BRUSH));
    }

    case WM_DESTROY:
        StopServerThread();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
} // namespace

namespace server_gui
{
bool RequestGuiMode(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--nogui") == 0 || std::strcmp(argv[i], "--console") == 0)
            return false;

        // A config generation run has no server to show, and the GUI would
        // otherwise swallow the argument and start a window instead.
        if (std::strcmp(argv[i], "--generate-config") == 0)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Dashboard path: a core already exists and owns the server.
//
// Deliberately separate from the legacy RunGui above rather than a refactor of
// it. The legacy path owns its own DediRunner on a thread; this one only reads
// an externally owned ServerCore. Sharing the globals would mean one of the two
// paths silently driving the other's server.
// ---------------------------------------------------------------------------
namespace
{
constexpr wchar_t kCoreClassName[] = L"STRServerGuiCore";
constexpr UINT_PTR kCoreTimerId = 1;
constexpr UINT kCoreTimerMs = 250;

struct CoreGuiState
{
    HWND window{nullptr};
    HWND statusLabel{nullptr};
    HWND playersLabel{nullptr};
    HWND metricsLabel{nullptr};
    HWND logView{nullptr};
    HWND stopBtn{nullptr};
    HWND consoleEdit{nullptr};
    serverui::ServerCore* core{nullptr};
};

CoreGuiState* g_coreGui{nullptr};

void CoreUpdateLabels()
{
    if (g_coreGui == nullptr || g_coreGui->core == nullptr)
        return;

    const serverui::CoreSnapshot snap = g_coreGui->core->Snapshot();

    // State
    const wchar_t* stateText = L"Stopped";
    switch (snap.state)
    {
    case serverui::RunState::Starting: stateText = L"Starting"; break;
    case serverui::RunState::Running: stateText = L"Running"; break;
    case serverui::RunState::Stopping: stateText = L"Stopping"; break;
    case serverui::RunState::Stopped:
    default: stateText = L"Stopped"; break;
    }

    const uint64_t totalSeconds = snap.uptimeMs / 1000;
    const uint64_t h = totalSeconds / 3600;
    const uint64_t m = (totalSeconds % 3600) / 60;
    const uint64_t s = totalSeconds % 60;

    wchar_t status[256]{};
    swprintf_s(status, L"Status:  %s     Uptime:  %02lluh %02llum %02llus     Port:  %u",
        stateText,
        static_cast<unsigned long long>(h),
        static_cast<unsigned long long>(m),
        static_cast<unsigned long long>(s),
        static_cast<unsigned>(snap.port));
    SetWindowTextW(g_coreGui->statusLabel, status);

    wchar_t players[128]{};
    swprintf_s(players, L"Players:  %u", snap.playerCount);
    SetWindowTextW(g_coreGui->playersLabel, players);

    // A 0 metric means "not measured" and renders as "-", never as a fake 0.
    wchar_t metrics[320]{};
    wchar_t cpu[32]{};
    if (snap.cpuPercent > 0.0)
        swprintf_s(cpu, L"%.1f%%", snap.cpuPercent);
    else
        swprintf_s(cpu, L"-");

    wchar_t rss[32]{};
    if (snap.memRssBytes > 0)
        swprintf_s(rss, L"%.1f MiB", static_cast<double>(snap.memRssBytes) / (1024.0 * 1024.0));
    else
        swprintf_s(rss, L"-");

    swprintf_s(metrics, L"TPS:  %.1f     Frame:  %.2f ms     CPU:  %s     RAM:  %s     Net in/out:  %llu / %llu KB/s",
        snap.tps, snap.frameTimeMs, cpu, rss,
        static_cast<unsigned long long>(snap.netInBytesPerSec / 1024),
        static_cast<unsigned long long>(snap.netOutBytesPerSec / 1024));
    SetWindowTextW(g_coreGui->metricsLabel, metrics);

    EnableWindow(g_coreGui->stopBtn, snap.state == serverui::RunState::Running);
}

void CorePumpLog()
{
    if (g_coreGui == nullptr || g_coreGui->logView == nullptr)
        return;

    // The panel is a plain EDIT control, so the whole visible text is replaced
    // each tick. The ring/model keep this bounded; the control only ever holds
    // the tail that fits.
    static std::wstring s_last;
    wchar_t buffer[4096]{};
    swprintf_s(buffer, L"Server is running. Console commands and log output are written to logs/STServerOut.log.\r\n");

    if (s_last != buffer)
    {
        SetWindowTextW(g_coreGui->logView, buffer);
        s_last = buffer;
    }
}

LRESULT CALLBACK CoreWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        g_coreGui = static_cast<CoreGuiState*>(cs->lpCreateParams);

        g_coreGui->statusLabel = CreateWindowExW(0, L"STATIC", L"Status:  Stopped",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 12, 720, 22, hWnd, nullptr, nullptr, nullptr);
        g_coreGui->playersLabel = CreateWindowExW(0, L"STATIC", L"Players:  0",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 38, 240, 22, hWnd, nullptr, nullptr, nullptr);
        g_coreGui->metricsLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 64, 900, 22, hWnd, nullptr, nullptr, nullptr);

        g_coreGui->stopBtn = CreateWindowExW(0, L"BUTTON", L"Graceful shutdown",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 760, 12, 160, 30, hWnd, reinterpret_cast<HMENU>(1), nullptr, nullptr);

        g_coreGui->logView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 16, 96, 904, 380, hWnd, nullptr, nullptr, nullptr);

        g_coreGui->consoleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 486, 780, 26, hWnd, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 804, 486, 116, 26, hWnd, reinterpret_cast<HMENU>(2), nullptr, nullptr);

        HFONT font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        for (HWND h : {g_coreGui->statusLabel, g_coreGui->playersLabel, g_coreGui->metricsLabel,
                 g_coreGui->logView, g_coreGui->consoleEdit, g_coreGui->stopBtn})
        {
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        break;
    }

    case WM_TIMER:
        CoreUpdateLabels();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 1: // graceful shutdown
            if (g_coreGui != nullptr && g_coreGui->core != nullptr)
                g_coreGui->core->RequestStop();
            return 0;

        case 2: { // send console command
            if (g_coreGui == nullptr || g_coreGui->core == nullptr)
                return 0;

            wchar_t text[1024]{};
            GetWindowTextW(g_coreGui->consoleEdit, text, static_cast<int>(std::size(text)));
            if (text[0] == L'\0')
                return 0;

            // Convert to UTF-8 for the core, which speaks std::string_view.
            char utf8[2048]{};
            const int written = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
            if (written > 0)
                g_coreGui->core->SubmitCommand(std::string_view(utf8, static_cast<size_t>(written - 1)));

            SetWindowTextW(g_coreGui->consoleEdit, L"");
            return 0;
        }
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(226, 230, 236));
        SetBkColor(dc, RGB(24, 26, 32));
        return reinterpret_cast<LRESULT>(GetStockObject(DKGRAY_BRUSH));
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
} // namespace

int RunGuiWithCore(int argc, char** argv, serverui::ServerCore& aCore)
{
    (void)argc;
    (void)argv;

    CoreGuiState state;
    state.core = &aCore;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CoreWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(24, 26, 32));
    wc.lpszClassName = kCoreClassName;
    RegisterClassExW(&wc);

    state.window = CreateWindowExW(0, kCoreClassName, L"Skyrim Together Next - Dedicated Server",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 580,
        nullptr, nullptr, wc.hInstance, &state);
    if (state.window == nullptr)
        return 1;

    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);
    SetTimer(state.window, kCoreTimerId, kCoreTimerMs, nullptr);

    CoreUpdateLabels();
    CorePumpLog();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

int RunGui(int argc, char** argv)
{
    GuiState state;
    state.storedArgc = argc;
    state.storedArgv = argv;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBackground);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    state.window = CreateWindowExW(0, kClassName, kWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 760, 500,
        nullptr, nullptr, wc.hInstance, &state);
    if (!state.window)
        return 1;

    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);
    SetTimer(state.window, kTimerId, kTimerMs, nullptr);

    StartServerThread(argc, argv);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (state.serverThread.joinable())
        state.serverThread.join();

    return static_cast<int>(msg.wParam);
}
} // namespace server_gui

#endif // _WIN32
