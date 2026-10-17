
# SERVER-UI-ARCHITECTURE — dual-mode (CLI / GUI) dedicated server + modern dark dashboard

Status: design spec (stream C). Target: C++20, x64, Windows primary, Linux headless.
Scope: `Code/server_runner/**` (existing panel), `Code/server_ui/**` (new tree), seam to `STServer.dll`.
**Nothing in this document has been compiled on this host: xmake is not installed here**
(`which xmake` -> not found). Every claim below is either a `file:line` citation from this
checkout or explicitly marked **TO VERIFY**.

Frozen companion contract: the `serverui` namespace
(`LogLevel / LogRecord / LogRing / RunState / CoreSnapshot / PlayerRow / ServerCore / LaunchMode`).

---

## 0 · Ten-line outline

| # | Section | What it settles |
|---|---|---|
| 1 | Current-state audit | 14 concrete defects with file:line and one-line consequence |
| 2 | Target architecture | component diagram, dependency-direction rule, the STServer.dll seam |
| 3 | Sequence diagrams | (a) start, (b) log line path, (c) graceful stop |
| 4 | Mode switching / no black window | subsystem flag, AttachConsole-vs-AllocConsole, exact xmake flags |
| 5 | Log pipeline | ring memory ordering, overflow, double-buffered drain, virtualization math |
| 6 | GUI layout + tech selection | ImGui+D3D11 decision, dark dashboard wireframe, 16 ms budget |
| 7 | Risk register + migration | R1–R12, M0–M9 independently shippable steps |
| 8 | What I would NOT build | YAGNI list |
| A | Appendix A | evidence index (every file read, every command run) |
| B | Appendix B | the 8 findings adjudicated + 10 findings you did not list |

---

## 1 · Current-state audit

### 1.1 What actually exists today

The feature is **not greenfield**. A working, dependency-free Win32 control panel already
runs the real server on a background thread:

* `Code/server_runner/gui/ServerGui.cpp` — 325 lines, pure `user32/gdi32` (`ServerGui.cpp:1-6`).
* It is compiled into the shipping runner on Windows only: `server_runner/xmake.lua:17-19`
  (`add_files("gui/ServerGui.cpp")`, `add_syslinks("gdi32")`).
* `main()` dispatches to it before anything else: `server_runner/main.cpp:225-226`.
* The server itself is created and driven on a `std::thread`:
  `ServerGui.cpp:118-132` -> `DediRunner` ctor (`DediRunner.cpp:30-47`) -> `RunGSThread()`
  (`DediRunner.cpp:138-150`).
* Telemetry crosses the DLL boundary through two `__declspec(dllimport)` functions:
  `ServerGui.cpp:46-47` <- `server/main.cpp:77-84` and `:86-90`.

So the honest starting point is: **a control panel exists, it is wired to the real server,
and its three load-bearing mechanisms (log transport, stop path, restart path) are all
wrong.** That is what this document replaces.

### 1.2 Defect table

Severity: **S1** = user-visible freeze/crash/data-loss, **S2** = feature does not work,
**S3** = design debt that blocks the target architecture.

| # | Sev | Defect | Evidence | Consequence |
|---|---|---|---|---|
| D1 | S1 | `PumpLog()` rebuilds the **entire** log into one `std::wstring` and pushes it with a synchronous `SetWindowTextW` every 500 ms, then issues two blocking `SendMessageW` (`EM_GETLINECOUNT`, `EM_LINESCROLL`) | `ServerGui.cpp:171-187` (rebuild loop `:176-181`, `SetWindowTextW` `:182`, `SendMessageW` `:185-186`), timer `:59-60`, `:230-233`, `:307` | Cost is O(history) per tick. 400 lines of wide text => full re-layout + full repaint of an `EDIT` control, on the UI thread. This is the freeze the user reported. |
| D2 | S1 | Every log line takes a `std::mutex` on the producer side, and the deque is silently truncated to 400 by `pop_front()` | `ServerGui.cpp:88-97` (`scoped_lock` `:93`, cap `:95-96`), member `:79-80` | Any thread that logs can block on the UI thread's `PumpLog` lock — a lock inversion with the join at `:318-319`. Truncation is invisible: no drop counter, no marker. |
| D3 | S2 | The panel's log is **not fed by the log stream**. `AppendLogLine` has exactly five call sites, all string literals | `ServerGui.cpp:119`, `:122`, `:129`, `:140` (definition `:88`) | The panel can never show a server message. The user sees 4 canned lines. |
| D4 | S2 | Real output goes to `stdout_color_sink` + `rotating_file_sink` on `globalOut`; a **second** logger `ConOut` carries all console-command output and is a separate logger | `server_runner/main.cpp:63-81` (ConOut `:63`, `globalOut` `:70-71`, `set_default_logger` `:78`, `SetDefaultLogger` `:81`); 15 `spdlog::get("ConOut")` call sites, e.g. `GameServer.cpp:313`, `:320`, `:339`, `:358`, `:375`, `:397`, `DediRunner.cpp:154` | A sink added to the default logger alone would still miss every `/players`, `/uptime`, `/admins` response. **Both** loggers need the sink. |
| D5 | S2 | The runner is a console-subsystem binary; a black window appears on every launch | `server_runner/xmake.lua:3-31` has no `/SUBSYSTEM`; the only real occurrence in the repo is `immersive_launcher/xmake.lua:16` (plus a *commented* one at `server_ui/xmake.lua:40`) | Double-click gives a console the user must not see. Link-time fact, unfixable at runtime. |
| D6 | S2 | Start works once per process; there is no in-process restart | `ServerGui.cpp:109-115` (`startRequested.exchange(true)` guard, comment "the runner cannot be constructed twice in one process"), `:167` disables the button, `:160-161` renders "restart the app to run again" | The requested Start / Restart hot path does not exist. The real reason is worse than the comment says — see **F9**. |
| D7 | S2 | Telemetry is one function, `QueryServerPlayerCount()`; no TPS, frame time, CPU, RSS, bandwidth or packet counters cross the boundary | `server/main.cpp:75-84`; no other `GS_EXPORT` telemetry (`server/main.cpp:65-122`) | The dashboard in §6 has no data source. `CoreSnapshot` fields `tps/frameTimeMs/cpuPercent/memRssBytes/net*/packets*` are unfillable today. |
| D8 | S1 | **Three data races on the current stop/telemetry path.** (a) `m_requestStop` is a plain `bool` written from the UI thread and read on the tick thread; (b) `PlayerManager::Count()` reads a container from the UI thread with no lock; (c) `GameServer::s_pInstance` is a raw pointer read from the UI thread | (a) `GameServer.h:143` + `GameServer.cpp:231-235` <- `server/main.cpp:86-90` <- `ServerGui.cpp:141`; read at `GameServer.cpp:571` / `GameServer.h:68`. (b) `ServerGui.cpp:164` -> `main.cpp:83` -> `PlayerManager.cpp:141-144`, container `PlayerManager.h:63`, no mutex anywhere in the class. (c) `GameServer.h:145`, `main.cpp:79` | UB on every 500 ms tick that overlaps a join/leave. A latent crash, and exactly what `ServerCore` is designed to remove. |
| D9 | S2 | Ctrl-C / console-close handling is disabled; the only shutdown path is the GUI's `RequestServerShutdown` | `server_runner/main.cpp:229-230` (`RegisterQuitHandler()` commented out, "started crashing on 1.7+") | Closing the console window of a CLI run does not shut the server down cleanly. |
| D10 | S3 | The EULA gate is dead code | `server_runner/main.cpp:146-196` defines `IsEULAAccepted()`; grep finds **no call site** | A shipped feature silently does nothing. Either wire it or delete it. |
| D11 | S3 | GUI mode is decided by an argv scan only; no environment probe, and the default is **GUI** | `ServerGui.cpp:268-281` returns `false` only for `--nogui/--console/--generate-config`, else `true` | A service, a scheduled task or a `cmd /c` run all get a window. Session 0 fails to create it (`CreateWindowExW` null -> `return 1`, `:302-303`) instead of falling back to CLI. |
| D12 | S3 | The runner's spdlog is header-only while the DLL's is compiled; the boundary works only because the *logger object* is handed across | `server_runner/xmake.lua:30` (`add_defines("SPDLOG_HEADER_ONLY")`) vs `server/xmake.lua:29-38`; handover `server/main.cpp:109-122`, adopted `server_runner/main.cpp:67`, `:81` | Any new sink must be created **inside the runner's** spdlog instance. Never pass a sink across the DLL boundary. |
| D13 | S3 | A new `.cpp` under `Code/server_runner/` is auto-compiled into the shipping binary | `server_runner/xmake.lua:13-14` (`add_files("**.cpp|gui/**.cpp")`); documented `server_ui/xmake.lua:9-12`, `server_ui/README.md:6-10` | The redesign cannot be landed in place. It needs the isolated tree that already exists. |
| D14 | S3 | `Code/xmake.lua` includes a fixed list; `server_ui` is not in it | `Code/xmake.lua:2-17` | The new tree is inert until one line is added. Good: it cannot break the live binary. |

### 1.3 What is NOT broken and must be preserved

| Asset | Evidence | Why it matters |
|---|---|---|
| `DediRunner` already separates "own the server" from "talk to a human" | `DediRunner.h:27-58` | `ServerCore` is a re-parenting of this class, not a rewrite. |
| The DLL seam is already a pure interface | `common/GameServerInstance.h:3-17` (5 virtuals) | `ICoreBackend` mirrors it 1:1 and adds two hooks. |
| Console commands + settings are already a registry with a lock | `ConsoleRegistry.h:36-102`, `ConsoleRegistry.cpp:223-274`, `:331-343` | The command console and the config panel are *views over an existing model*. Do not invent a second command system. |
| Settings are already enumerable with name + description | `ConsoleRegistry.h:76-80` (`ForAllSettings`), `Setting.h:114-115` | The config panel is a generic loop, not hand-written per field. |
| A lock-free MP/SC queue already exists in-repo with the exact acquire/release shape needed | `base/containers/fast_queue.hpp:98-141` | Prior art for `LogRing`; copy the ordering, **not** the blocking read (`:126-127` sleeps 1 ms — unacceptable on the UI thread). |
| ImGui is already vendored with the two backends a Win32 host needs | `xmake.lua:53-58` (imgui v1.89.7), `components/imgui/imgui_impl_win32.cpp` (670 lines), `imgui_impl_dx11.cpp` (687 lines), `components/imgui/xmake.lua:2-5` | The GUI stack decision in §6 is mostly "use what is already here". |
| A virtualized ImGui console is prior art | `admin/Widgets/Console.cpp:69-98` (`ImGuiListClipper`), history callback `:19-53` | Copy the clipper pattern verbatim. |
| A dark ImGui style + DPI scaling is prior art | `components/imgui/ImGuiDriver.cpp:14-30` (style), `:112-126` (`GetDpiScaleForHwnd` + `ScaleAllSizes`) | The "modern dark" look is a style block, not a project. |
| The manifest already declares PerMonitorV2 DPI awareness | `server_runner/resources/server.manifest:18-19`, embedded by `server_runner.rc:32` | Reuse it; do not add a second manifest. |

---

## 2 · Target architecture

### 2.1 Component diagram

```
                        +----------------------------------------------+
                        |                  main()                      |
                        |  ResolveLaunchMode -> EnsureConsoleForMode   |
                        +--------------+-------------------------------+
                                       | LaunchMode
             +-------------------------+---------------------------+
             |                         |                           |
      +------v------+           +------v------+            +-------v--------+
      | CLIController|          | GUIController|            |  Headless      |
      | (stdin loop) |          | (Win32+D3D11)|            |  (no console)  |
      +------+------+           +------+------+            +-------+--------+
             |                         |                           |
             | reads LogModel          | reads LogModel,           |
             | writes SubmitCommand    | CoreSnapshot, Players(),  |
             |                         | SubmitCommand, ApplySet   |
             +--------------+----------+---------------+-----------+
                            |                          |
                   +--------v---------+       +--------v---------+
                   |    LogModel      |<------|  LogRing::Drain  |
                   | (virtualized VM) | copies|  (UI thread only)|
                   +------------------+       +--------^---------+
                                                       | Push (never blocks)
                   +-------------------------------+   |
                   | LogSink : spdlog::sinks::sink |---+
                   |  (lives in runner's spdlog)   |
                   +---------------+---------------+
                                   | sink_it_ on the CALLER thread
        +--------------------------+-------------------------------+
        |                    ServerCore (facade)                   |
        |  one core thread . atomic snapshot publish . cmd queue   |
        +--------------------------+-------------------------------+
                                   | ICoreBackend (5 virtuals + 2 hooks)
        +--------------------------v-------------------------------+
        |              DediRunner / IGameServerInstance            |
        |  Initialize . Update . Shutdown . IsListening . Players  |
        +--------------------------+-------------------------------+
                                   | GS_IMPORT exports (process boundary)
+==================================v===================================+
|  STServer.dll                                                       |
|  CreateGameServer . RegisterServerSettings . CheckBuildTag          |
|  SetDefaultLogger . RegisterLogger . QueryServerPlayerCount         |
|  RequestServerShutdown                                              |
|      +-- GameServer : TiltedPhoques::Server                         |
|            +-- World -> PlayerManager (Count / ForEach)             |
|            +-- ConsoleRegistry (TryExecuteCommand / Update /        |
|            |                    ForAllCommands / ForAllSettings)    |
|            +-- GetUptime() . GetAdminSessions()                     |
+======================================================================+
```

### 2.2 Dependency-direction rule (the one rule that keeps this maintainable)

**UI depends on core. Core depends on nothing. The DLL is reached only through one
interface and one set of C-linkage exports.**

| Layer | May include | Must NOT include | Enforcement |
|---|---|---|---|
| `LogRing`, `LogModel`, `LaunchMode`, `ConsoleHost` | `<atomic> <chrono> <cstdint> <vector> <string_view>` | `spdlog`, `windows.h`, `uv.h`, `GameServer.h` | Builds and tests standalone (`server_ui/README.md:21-30`). Verified on this host: `g++ -std=c++20 -fsyntax-only` is clean on `ServerCore.cpp`, `LaunchMode.cpp`, `ConsoleHost.cpp`. |
| `ServerCore` | the above + `ICoreBackend` | any UI toolkit, any Win32 handle | `ServerCore.h:1-8` states this contract; `ServerCore.h:23-38` only uses STL. |
| `LogSink` | `spdlog/sinks/base_sink.h`, `LogRing` | `windows.h`, any `HWND` | The sink runs on arbitrary producer threads; a UI handle there is the freeze bug in another costume. |
| `GUIController` | Win32 + D3D11 + ImGui + `LogModel` + `ServerCore` | `GameServer.h`, spdlog sinks | Mirrors the existing rule at `ServerGui.cpp:38-40` ("the gui must not reach into GameServer.h directly"). |
| `ICoreBackend` impl (`DediBackend`) | `DediRunner`, `GameServerInstance.h` | UI, ImGui | It is the *only* place the two worlds meet. |

Two rules fall out of the frozen contract and are not negotiable:

1. **`LogRecord::text` is valid only until the next `Drain()`.** Therefore `LogModel` must
   copy payload bytes during drain. A UI that stores the `const char*` renders freed
   memory one frame later. Contract constraint, not an implementation choice.
2. **`Players()` returns `PlayerRow::username` stable for the lifetime of the returned
   vector.** The WIP core solves it with an append-only `std::deque<std::string>` arena
   (`ServerCore.h:246-253`) — keep that shape; do not switch `PlayerRow` to `std::string`,
   which would change the frozen contract.

### 2.3 Thread model

| Thread | Count | Owns | Calls |
|---|---|---|---|
| UI thread | 1 | `HWND`, D3D11 device, `LogModel`, ImGui context | `LogRing::Drain`, `ServerCore::Snapshot/Players/SubmitCommand/RequestStop`, `LogModel` reads |
| Core thread | 1 | `IGameServerInstance`, `ConsoleRegistry::Update` | `Initialize/Update/Shutdown`, `OnCommand`, `Players()` |
| Producer threads | N (network, scripts, DLL) | nothing shared | `LogRing::Push` only |
| Render thread | same as UI (single-threaded ImGui) | — | — |

`Snapshot()` / `Players()` are the only cross-thread reads, and both return by value from
atomically published immutable state (`ServerCore.h:232-234`). The UI thread takes no mutex
while drawing.

---

## 3 · Sequence diagrams

### 3.1 (a) Process start -> mode resolution -> console decision -> core start

```
main(argc, argv)
  |
  +- CheckBuildTag(kBuildTag) ------------ mismatch -> return 1        [runner main.cpp:214]
  |
  +- LogInstance{} ---------------------- creates logs/ dir, ConOut,
  |                                        globalOut(stdout+file)      [main.cpp:52-85]
  |                                        <- LogSink must be added to BOTH
  |                                           loggers HERE, before SetDefaultLogger
  |
  +- mode = ResolveLaunchMode(argc, argv, "config/STServer.ini")
  |     precedence: --cli|--console|--nogui -> Cli
  |                 --headless             -> Headless
  |                 --gui                  -> Gui
  |                 --generate-config      -> GenerateConfig
  |                 ini launch/mode=<m>
  |                 Windows: IsGraphicalEnvironment() ? Gui : Cli
  |
  +- switch (mode)
  |    +- GenerateConfig: write default ini -> return 0  (no server, no window)
  |    +- Cli:      EnsureConsoleForMode(Cli)
  |    |              AttachConsole(ATTACH_PARENT_PROCESS)
  |    |                ok   -> freopen_s(CONIN$/CONOUT$/CONOUT$)
  |    |                fail -> AllocConsole() + freopen_s + SetConsoleCP/OutputCP(UTF8)
  |    |              SetConsoleCtrlHandler(...)      <- D9: reinstall, it is off today
  |    +- Gui:      EnsureConsoleForMode(Gui) -> FreeConsole() if attached
  |    +- Headless: no console work at all
  |    +- CLIController / GUIController constructed with &core
  |
  +- ServerCore core{config, make_unique<DediBackend>()}
  |     +- core.Start()
  |     |    spawn thread -> DediBackend::Initialize()
  |     |      -> DediRunner(argc, argv)
  |     |          -> RegisterServerSettings(reg)   [DediRunner.cpp:38]
  |     |          -> LoadSettings(argc, argv)      [DediRunner.cpp:39, 56-88]
  |     |          -> CreateGameServer(reg)         [DediRunner.cpp:42]
  |     |          -> instance->Initialize()        [DediRunner.cpp:45]
  |     |          -> SaveSettingsToIni(...)        [DediRunner.cpp:46]
  |     |    publish state = Running, signal Start()
  |     +- return true
  |
  +- controller.Run()
  |    +- Cli:      blocking loop, stdin lines -> SubmitCommand
  |    +- Gui:      Win32 message pump + D3D11 frame loop
  |    +- Headless: wait on stop event
  +- controller.Run() returns
       -> core.RequestStop()  (idempotent, joins)     [ServerCore.h:166-169]
       -> ReleaseConsole()
       -> return 0
```

Ordering constraint: `LogInstance` is constructed **before** mode resolution
(`main.cpp:219` precedes `:225`). That is correct and must stay — a mode-resolution failure
has to be loggable. But `ConfigureConsoleMode()` currently runs first of all
(`main.cpp:211`) and calls `SetConsoleOutputCP`/`SetConsoleCP` on a process that may have no
console at all. Both fail silently there. **They must move into `EnsureConsoleForMode`**,
after the console exists.

### 3.2 (b) A log line: tick thread -> ring -> UI drain -> virtual render

```
 TICK THREAD                              UI THREAD
 -----------                              ---------
 spdlog::info("...")
   | logger->sink_it_(msg) for each sink
   |   +- stdout_color_sink  -> CONOUT$ (CLI) / void (GUI)
   |   +- rotating_file_sink -> logs/STServerOut.log
   |   +- LogSink::sink_it_(msg)          <- added in LogInstance
   |        +- lvl  = map(msg.level)
   |        +- ts   = steady_clock ms
   |        +- tid  = GetCurrentThreadId()
   |        +- fmt into a stack buffer (no heap)
   |        +- ring.Push(lvl, tid, ts, buf, len)
   |             seq = m_reserve.fetch_add(1)           [relaxed]
   |             slot = seq & (cap-1)
   |             wait until slot.state == 2*round      [acquire]
   |             memcpy header+payload
   |             slot.state.store(2*round+1, release)  [publish]
   |             false -> m_dropped.fetch_add(1)        (drop, never block)
   +- returns; producer never touches a UI handle
                                          frame tick (<= 16 ms)
                                            |
                                            +- n = ring.Drain(scratch, kMaxBatch)
                                            |     head = m_head.load(acquire)
                                            |     for i in [tail, head):
                                            |        if slot.state == 2*round+1
                                            |           copy LogRecord + payload
                                            |        else break   (not yet published)
                                            |     m_tail.store(head, release)
                                            |
                                            +- LogModel::Commit(scratch, n)
                                            |     copy each payload into a slab
                                            |     append fixed-size row index
                                            |     insert "k dropped" row on a seq gap
                                            |     publish row count (atomic)
                                            |
                                            +- draw
                                                 ImGuiListClipper.Begin(rows)
                                                 while Step():
                                                   for i in [DisplayStart, DisplayEnd):
                                                      row = model.RowAt(i)   O(1)
                                                      TextUnformatted(row.text)
```

### 3.3 (c) GUI "Graceful shutdown" -> graceful shutdown -> join

```
 UI THREAD                                     CORE THREAD
 ---------                                     -----------
 [Graceful shutdown] clicked
   |
   +- core.RequestStop()
   |    m_stopRequested.store(true, release)
   |    m_wakeCv.notify_all()  ---------------> wakes from wait_for(tick period)
   |    lock(m_joinMutex); if (joinable) join() -+
   |    (later calls: no-op)                     |
   |                                             | loop condition sees stop
   |                                             +- DediBackend::Shutdown()
   |                                             |    -> instance->Shutdown()
   |                                             |    -> GameServer::Kill()
   |                                             |         m_requestStop = true  [GameServer.cpp:234]
   |                                             |         spdlog::info("Server shutdown requested")
   |                                             +- one final Update()
   |                                             |    -> OnUpdate() sees m_requestStop
   |                                             |    -> Close()                  [GameServer.cpp:571-572]
   |                                             |    -> IsListening() == false
   |                                             +- publish state = Stopping -> Stopped
   |                                             +- thread function returns
   |<--------------------------------------------+
   +- join() returns  (<= one tick + shutdown cost)
   +- state = Stopped; Start/Restart re-enabled
   +- window stays open; process exit only on WM_CLOSE
```

The **critical difference from today**: the stop request is an atomic flag consumed by the
core thread, and the UI thread *joins* instead of firing a call into server state
(`ServerGui.cpp:141` -> `server/main.cpp:89` -> `GameServer.cpp:234`, defect D8a).

---

## 4 · Mode switching and the no-black-window mechanism

### 4.1 The two subsystem flags

| Flag | What Windows does at process start | Black window? |
|---|---|---|
| `/SUBSYSTEM:CONSOLE` (current, implicit) | Allocates a console **before** `main()` runs; `stdin/stdout/stderr` are already bound to it | **Always.** Double-click, cmd, service, task scheduler — no opt-out. |
| `/SUBSYSTEM:WINDOWS` | Allocates **nothing**. No console, no `stdin/stdout` handles; `printf` goes nowhere | Never, unless we ask for one |

Because the current runner is `CONSOLE` and has no `/SUBSYSTEM` anywhere
(`server_runner/xmake.lua:3-31`; repo-wide grep for `SUBSYSTEM` finds only
`immersive_launcher/xmake.lua:16` and a commented line at `server_ui/xmake.lua:40`), the
black window is a **link-time** fact and cannot be fixed at runtime. This is finding F5,
CONFIRMED.

### 4.2 Exact xmake linker settings

```lua
-- Code/server_ui/xmake.lua  (the target is currently commented out at :35-43)
target("SkyrimTogetherServerUi")
    set_kind("binary")
    set_basename("SkyrimTogetherServer")      -- same name as the shipping runner
    set_languages("cxx20")
    add_files("src/**.cpp|tests/**.cpp", "gui/**.cpp")
    add_deps("SkyrimTogetherServer")          -- STServer.dll (Code/server/xmake.lua:41-45)
    add_packages("tiltedcore", "spdlog", "libuv", "imgui")
    if is_plat("windows") then
        -- THE fix: windowed subsystem + our own entry point
        add_ldflags("/SUBSYSTEM:WINDOWS", "/ENTRY:mainCRTStartup", { force = true })
        add_files("server_runner.rc")         -- reuse manifest + icon
        add_syslinks("user32", "gdi32", "shell32", "d3d11", "dxgi")
    end
```

Why each piece:

| Setting | Reason | In-repo precedent |
|---|---|---|
| `/SUBSYSTEM:WINDOWS` | Removes the console allocation at process start. The only way to get "no black window on double-click". | `immersive_launcher/xmake.lua:16` |
| `/ENTRY:mainCRTStartup` | With `WINDOWS` the linker looks for `WinMain`. Our entry is `int main(int, char**)` (`server_runner/main.cpp:209`) and must stay `main` so the same source runs on Linux and so the console decision happens *after* argv parsing. | `immersive_launcher/xmake.lua:17` |
| `{ force = true }` | xmake injects its own subsystem default; without `force` the flag is not last on the link line. | same line |
| `d3d11`, `dxgi` | ImGui's vendored renderer backend (`components/imgui/imgui_impl_dx11.cpp:37` includes `<d3d11.h>`). | — |
| reuse `server_runner.rc` | Already embeds `resources/server.manifest` with PerMonitorV2 DPI awareness (`server_runner.rc:32`, `resources/server.manifest:18-19`) and the icon (`server_runner.rc:34`). A second manifest would be a second DPI story. | — |

Also: keep the `SPDLOG_HEADER_ONLY` decision consistent with the final seam — see R2 in §7.
The current runner defines it (`server_runner/xmake.lua:30`) precisely because it links the
DLL's spdlog separately.

### 4.3 Console state machine

`EnsureConsoleForMode(LaunchMode)` — idempotent, noexcept, no-op off Windows.

| Mode | Sequence | Result |
|---|---|---|
| `Cli` | 1. `AttachConsole(ATTACH_PARENT_PROCESS)`<br>2. if it fails -> `AllocConsole()`<br>3. `freopen_s(&f,"CONIN$","r",stdin)`<br>4. `freopen_s(&f,"CONOUT$","w",stdout)`<br>5. `freopen_s(&f,"CONOUT$","w",stderr)`<br>6. `SetConsoleCP(CP_UTF8)`, `SetConsoleOutputCP(CP_UTF8)`<br>7. `SetConsoleCtrlHandler` | Writes into the user's existing cmd window when there is one, otherwise owns a new one. |
| `Gui` | 1. `FreeConsole()` if `GetConsoleWindow() != nullptr`<br>2. nothing else | A GUI launch never leaves a stray console behind, including "was `--cli`, then switched" and "launched under a debugger". |
| `Headless` | nothing | Logs go to `logs/STServerOut.log` only. No console ever exists, so no window can appear. |
| `GenerateConfig` | `Cli` rules (it prints one line, `DediRunner.cpp:65`) then exits | — |

### 4.4 Why `AttachConsole` **must** be tried before `AllocConsole`

A process can be attached to **at most one** console. `AllocConsole()` gives the process its
own console immediately; after that, `AttachConsole(ATTACH_PARENT_PROCESS)` fails with
`ERROR_ACCESS_DENIED` because the process already has one. So the wrong order reproduces
exactly the bug we are trying to remove:

```
   cmd.exe  --spawn-->  app.exe --cli
                          +- AllocConsole()          -> SECOND black window appears
                          +- AttachConsole(parent)   -> ERROR_ACCESS_DENIED (ignored)
                                                        output goes to the NEW window,
                                                        the user's cmd shows nothing
```

The correct order tries the cheap, non-destructive operation first:

```
   cmd.exe  --spawn-->  app.exe --cli
                          +- AttachConsole(parent) -> SUCCESS: we write into that window
                          +- (AllocConsole never called)
   explorer --dblclick--> app.exe --cli
                          +- AttachConsole(parent) -> ERROR_INVALID_HANDLE (explorer has none)
                          +- AllocConsole()        -> one window, ours, correct
```

Three consequences that are easy to miss, and are all bugs in a naive implementation:

1. **`freopen_s` is mandatory, not cosmetic.** A `/SUBSYSTEM:WINDOWS` process starts with
   `stdin/stdout/stderr` **unbound** (the CRT initialised them against invalid handles).
   A successful `AttachConsole` gives the process a console but does **not** rebind the C
   streams. Without the three `freopen_s` calls, `printf`, `std::cout` and spdlog's
   `stdout_color_sink` (`server_runner/main.cpp:63`, `:70`) write into nothing even though a
   console exists. This is the single most common way this feature is half-implemented.
2. **`SetConsoleOutputCP` must run after the console exists.** Today it runs first
   (`main.cpp:211`) and fails silently in a windowed build.
3. **cmd.exe does not wait for a `/SUBSYSTEM:WINDOWS` child.** `app.exe --cli` returns the
   prompt immediately while the server keeps running and keeps writing into that window.
   Document `start /wait app.exe --cli` (or a `.bat` wrapper) for scripted use. Do not try
   to defeat this — it is a property of the subsystem, not of our code.

### 4.5 The six launch cases, end to end

| Case | Console at start | Decision | Console after | Window |
|---|---|---|---|---|
| Double-click in Explorer | none (windowed subsystem) | `IsGraphicalEnvironment()` true -> `Gui` | none | dashboard |
| `app.exe --cli` in cmd | parent's console exists, **not** inherited | argv -> `Cli` | attached to parent via `ATTACH_PARENT_PROCESS` | none |
| Double-click a `--cli` shortcut | none | argv -> `Cli` | `AllocConsole()` — one console, ours | none |
| Service / Session 0 / disconnected RDP | none; no usable window station | `IsGraphicalEnvironment()` false -> `Cli` (or `--headless`) | `AllocConsole()` fails in Session 0 -> file logging only | none |
| `--headless` | none | argv -> `Headless` | untouched | none |
| `--generate-config` | none | argv -> `GenerateConfig` | CLI rules, prints one line, exits | none |

`IsGraphicalEnvironment()` must be conservative: a null or non-interactive window station
means "no window". The WIP probe already does this (`server_ui/src/launch/LaunchMode.cpp:137`
`GetProcessWindowStation`, `:216`) and is documented at `LaunchMode.h:40-43`. **The current
panel has no such probe at all** — it returns `true` by default (`ServerGui.cpp:268-281`),
which is defect D11.

### 4.6 Linux equivalent

There is no subsystem concept and no console to hide.

| Aspect | Linux behaviour |
|---|---|
| "Black window" | Does not exist. A terminal is inherited via fd 0/1/2 from the parent shell; nothing is allocated. |
| `Cli` | Use fd 0/1/2 as inherited. If `isatty(0)` is false, disable the interactive loop (equivalent of `bConsole`, `DediRunner.cpp:23`, `:143`) and log to file. |
| `Gui` | **Not supported.** The panel is `#ifdef _WIN32` (`ServerGui.cpp:10`, `ServerGui.h:14-15`, `server_runner/xmake.lua:17-20`). `ResolveLaunchMode` must fall back to `Cli` on non-Windows (`LaunchMode.h:36`). |
| `Headless` | Run under systemd `Type=simple` with `StandardOutput=journal`; no `freopen`, no `setsid`, no double-fork daemonisation (YAGNI — see §8). |
| Ctrl-C | `signal(SIGINT/SIGTERM)` already exists (`server_runner/main.cpp:114-124`) but is **not installed**, because the whole `RegisterQuitHandler()` call is commented out (`:229-230`, defect D9). |

---

## 5 · Log pipeline design

### 5.1 Why the producer must never touch a UI handle

`LogSink::sink_it_` runs on **whatever thread called `spdlog::info`** — the tick thread, a
network thread inside the DLL, a script callback. Any Win32 call that targets a window from
a non-owner thread is a cross-thread `SendMessage`; it **blocks until the UI thread pumps**,
and the UI thread is the one that joins the core thread in `RequestStop`. That is a textbook
deadlock, and it is already half-present today: `PumpLog` issues `SendMessageW` from the UI
thread (`ServerGui.cpp:185-186`) while `StopServerThread` (`:135-142`) and the join at
`:318-319` depend on the core thread finishing. The existing `PostMessageW` (`:130`) shows
the author knew this; the fix is to make the rule absolute: **the log path touches only
`LogRing`**.

Cost per line on the producer: one `fetch_add`, one `memcpy` into a pre-allocated arena, one
release store. No allocation, no lock, no syscall, no throw — exactly what the frozen
contract's `Push` promises.

### 5.2 Memory ordering: reserve -> write -> publish

`LogRing` is **multi-producer / single-consumer**, fixed capacity, power-of-two, with a
per-slot state word. The in-repo `fast_queue` already implements this exact protocol and is
the reference to copy:

| Step | Operation | Ordering | Reference |
|---|---|---|---|
| Reserve | `seq = m_reserve.fetch_add(1)` | `relaxed` (uniqueness only) | `fast_queue.hpp:100` |
| Wait for slot to be free | spin on `slot.state == 2*round` | `acquire` | `fast_queue.hpp:103` |
| Write | header + payload `memcpy` | plain | `fast_queue.hpp:106` |
| Publish | `slot.state.store(2*round + 1, release)` | **`release`** | `fast_queue.hpp:117` |
| Consumer claims | `head = m_head.load(acquire)` | `acquire` | `fast_queue.hpp:126` |
| Consumer done | `slot.state.store(2*round + 2, release)` | `release` | `fast_queue.hpp:140` |

The `release`/`acquire` pair on `slot.state` is what makes the payload bytes visible to the
consumer: the release store orders the `memcpy` before the state transition, and the acquire
load orders the state read before the payload read. A plain `bool` "ready" flag would let
the consumer read a torn payload on a weakly-ordered target.

Two deliberate departures from `fast_queue`:

1. **Never block the consumer.** `fast_queue::read_acquire` spins with `sleep_for(1ms)`
   (`fast_queue.hpp:126-127`) and the writer yields (`:103-104`). Both are unacceptable
   here: the consumer is the UI thread inside a 16 ms frame, and the producer is the tick
   thread. `Drain` reads a **published watermark** and stops at the first unpublished slot —
   it never waits. A slot that is reserved but not yet published is simply not consumed
   this frame.
2. **Drop instead of stall.** The producer must never wait for space; when the ring is full,
   `Push` returns `false` and increments `DroppedCount` (contract). Because `seq` is still
   consumed from the reservation counter, **a gap in `seq` is the exact, cheap, race-free
   signal that records were lost** — no separate bookkeeping and no way to miss it.

### 5.3 Layout and capacity

```
 arena (single malloc at construction, never grows)
 +--------+--------------+--------+--------------+-----
 | hdr(0) | payload(0)   | hdr(1) | payload(1)   | ...
 +--------+--------------+--------+--------------+-----
   hdr = { uint64 seq; uint8 level; uint8 pad[3];
           uint64 tsMs; uint32 tid; uint32 len; atomic<uint32> state; }
```

* `LogRecord` (the contract POD) is produced by `Drain` from `hdr` + a pointer into the arena.
  It is 40 bytes on x64 and trivially copyable, so `Drain` is a `memcpy` loop.
* Records are variable-length; a record whose payload does not fit in the remaining
  contiguous space is **dropped**, not split. Simplest correct rule; splitting costs a
  consumer-side reassembly state machine for no benefit.
* Sizing: `capacityBytes = 4 MiB` ≈ 34k lines at ~120 B/line. At `info` level the server emits
  a handful of lines per second, so this is many minutes of headroom; at `trace` it is
  seconds, and the drop counter tells the user exactly that. One number, one trade-off, no
  tuning knobs.
* `kMaxRecordLen = 4096`. Longer messages are truncated with a marker. This bounds the worst
  case for one `memcpy` on the tick thread.

### 5.4 Overflow policy (explicit, and visible)

| Situation | Behaviour | UI rendering |
|---|---|---|
| Ring has space | record stored, `TotalCount++` | normal row |
| Ring full / payload too large | record dropped, `DroppedCount++` | synthetic row "… 137 records dropped …" inserted at the `seq` gap |
| Producer reserved but not yet published | not drained this frame | appears next frame (<= 16 ms latency) |
| Consumer slower than producer for a long time | drops grow monotonically | the marker row shows the running total |

Silent truncation — `pop_front()` at `ServerGui.cpp:95-96` — is the one policy that is
forbidden, because it makes "the log is missing lines" indistinguishable from "the server
said nothing".

### 5.5 Double-buffered drain

`LogRecord::text` is valid **only until the next `Drain()`**. Three buffers, three roles:

| Buffer | Owner | Lifetime | Purpose |
|---|---|---|---|
| ring arena | `LogRing` | until the slot is recycled | producer writes, consumer reads once |
| `scratch[]` (fixed `LogRecord` array, on the UI stack or a member) | UI thread | one frame | `Drain` output; payload pointers point into the ring |
| `LogModel` slab (append-only arena, periodic compaction) | UI thread | until the row scrolls out of the model | **the copy that makes rows survive the next `Drain()`** |

Sequence per frame, in this exact order:

1. `n = ring.Drain(scratch, kMaxBatch)` — pointers valid now, invalid after step 3.
2. `model.Commit(scratch, n)` — copies each payload into the slab, appends a fixed-size
   `RowRef { const char* text; uint32 len; uint8 level; uint64 tsMs; uint32 tid; }` to a ring
   of rows, records drop markers from `seq` gaps.
3. Next `Drain` may now recycle slots. Nothing in the model points into the ring.
4. Draw reads only the model.

`kMaxBatch` is bounded (e.g. 4096) so one pathological burst cannot blow the frame budget;
the remainder is drained next frame. Draining is O(batch), not O(history).

### 5.6 Virtualization math

Only the rows inside the clip rectangle are formatted and submitted.

```
 p  = ImGui::GetTextLineHeightWithSpacing()      // row pitch, px (~19 at 100% DPI)
 H  = clip rect height, px                       // log pane height (~420)
 V  = ceil(H / p) + 1                            // visible rows  (~23)
 S  = scroll position, px
 first = floor(S / p)                            // first visible row index
 last  = first + V + 1                           // one row of overscan
 rows formatted per frame = last - first ~= V + 1
```

At `p = 19`, `H = 420` -> **<= 24 rows formatted per frame**, regardless of whether the model
holds 400 or 4,000,000 rows. At a generous ~2 µs per row (format the timestamp, level tag,
thread id and text into a fixed 256-byte scratch, then one `ImGui::TextUnformatted`), that
is **<= 48 µs = 0.3 % of a 16.6 ms frame**.

Contrast with today: `PumpLog` concatenates every stored line and hands the result to
`SetWindowTextW` (`ServerGui.cpp:176-182`). That is O(N) in history, plus a full `EDIT`
re-layout, plus two more `SendMessageW` round trips (`:185-186`) — on a 500 ms timer, on the
UI thread. It is not that 400 lines is slow; it is that the cost is **unbounded in the wrong
variable**, and the whole window stops responding while it runs.

**O(1) row lookup** is a hard requirement, not an optimisation: `RowAt(i)` is called inside
the clipper loop for arbitrary `i`, and `ImGuiListClipper` itself does O(1) index math
(`admin/Widgets/Console.cpp:71-74` shows the shape). The model is a power-of-two ring of
fixed-size `RowRef`:

```cpp
const RowRef& RowAt(size_t i) const noexcept { return m_rows[(m_head + i) & m_mask]; }
```

No list traversal, no `std::deque` chunk walk, no per-row allocation. Text storage is a
separate append-only slab; `RowRef` holds `(offset, length)` into it, so a row is 24 bytes and
model memory is `rows * 24 + text bytes`.

Two derived behaviours, for free:

* **Follow-tail** is `scrollY = max(0, rowCount * p - H)` — O(1).
* **"Jump to timestamp"** would be a binary search over a monotonic `tsMs` array — O(log n).
  Not in v1 (§8).

---

## 6 · GUI layout and technology selection

### 6.1 Recommendation

**Win32 host window + D3D11 + Dear ImGui v1.89.7 (already-vendored backends), single
viewport, no docking branch, no extra dependency.**

| Candidate | Verdict | Evidence / reasoning |
|---|---|---|
| **ImGui + Win32 + D3D11** | **Recommended** | `imgui v1.89.7` is already an `add_requires` on Windows (`xmake.lua:53-58`). Both backends a Win32 host needs are already vendored in-tree: `components/imgui/imgui_impl_win32.cpp` (670 lines) and `imgui_impl_dx11.cpp` (687 lines), built by `components/imgui/xmake.lua:2-5`. A dark style + DPI scaling already exists (`ImGuiDriver.cpp:14-30`, `:112-126`). The exact widget we need — a virtualized scrolling console — already exists (`admin/Widgets/Console.cpp:69-98`). Plot widgets (`ImGui::PlotLines`) come free; hand-rolling a time-series chart in GDI is a week. |
| Qt 6 | **Rejected** | Absent from the repository. Repo-wide grep for `Qt6/Qt5/QApplication/find_package(Qt` -> **0 hits**. Adding it means a multi-hundred-MB dependency, a new build story inside xmake, and a licensing review, to a binary that currently links `user32/gdi32` only (`server_runner/xmake.lua:19`). Nothing in the requirements needs a widget set that large. |
| Magnum (the `Code/admin` route) | **Rejected** | `AdminApp.h:3,5` pull `Magnum/ImGuiIntegration` **and** `Magnum/Platform/Sdl2Application`. The magnum imgui config is *commented out* in the root build (`xmake.lua:69-74`), and `Code/admin/xmake.lua:24-31` lists neither `sdl2` nor `imgui`. So this path is not a validated reference on this host — **TO VERIFY** whether `Admin` builds today. It is also heavier than the panel needs. |
| Keep pure Win32 owner-draw | **Rejected as the target**, kept as the fallback | Zero dependencies (today's virtue, `ServerGui.cpp:1-6`), but a player table needs `LVS_OWNERDATA` gymnastics, a TPS curve needs a custom `WM_PAINT` path, and dark theming needs owner-draw for every control (`ServerGui.cpp:249-254` already shows the limit: `WM_CTLCOLORSTATIC` only). Strictly more code than ImGui for strictly less capability. |
| Web UI (Electron / localhost) | **Rejected** | Ships a browser to draw a table, and breaks the "one process, no IPC" property. |

Risk accepted with this choice: D3D11 device loss (TDR / driver update). Handle
`DXGI_ERROR_DEVICE_REMOVED` from `Present` by dropping the device, recreating it, and
re-uploading the ImGui font atlas. ~40 lines, once. If that is judged too much for v1, the
documented fallback is the Win32 owner-draw panel — **not** Qt.

### 6.2 Wireframe — Modern Dark Dashboard

```
+--------------------------------------------------------------------------------------+
| * RUNNING    uptime 02:14:37    port 10578    players 7/32    12.4 ms/frame          |
|                                            [Start] [Restart] [Graceful shutdown]     |
+-------------------------------------------+------------------------------------------+
| THROUGHPUT                                | RESOURCES                                |
|  TPS 59.8 / 60      frame 12.4 ms         |  CPU  18.3 %   -------------___         |
|  +------------------------------+         |  RAM  412 MB   -------/                  |
|  |      /    /___             |  TPS    |                                          |
|  | ----/  __/     __/__      |  60     | NET  v 128.4 KB/s   ^ 96.2 KB/s          |
|  +------------------------------+         |      +------------------------+          |
|  +------------------------------+         |      |  v_-^-~-  ^_--~-_      |          |
|  | -----___/----__  (ms)      |  16 ms  |      +------------------------+          |
|  +------------------------------+         | PKT  in 41 208/s   out 38 900/s          |
+-------------------------------------------+------------------------------------------+
| PLAYERS                                                          filter: [________]  |
|  #  name            ping   endpoint              state     connected   flags         |
|  1  Dragonborn__    32 ms  203.0.113.7:52144     in-world  00:41:12    ADMIN         |
|  2  Lydia          118 ms  198.51.100.22:51002   loading   00:02:55                  |
|  3  ...                                                                              |
|                        (right-click a row -> Kick / Ban / Direct message)            |
+--------------------------------------------------------------------------------------+
| CONSOLE                                              [level v info] [x follow tail]  |
| 02:14:31 [info ] [t 4120] New player 3f 'Dragonborn__' authenticated                |
| 02:14:37 [warn ] [t 4120] Port 10578 is already in use, trying 10579                |
| > /players_                                                                          |
| 02:14:38 [info ] [t 0001] <------Players-(2)--->                                     |
+--------------------------------------------------------------------------------------+
| CONFIG  config/STServer.ini        [Reload from disk] [Apply] [Open in editor]       |
|  GameServer  uPort            10578        > restart required                        |
|  GameServer  uMaxPlayerCount  8            > live                                    |
|  GameServer  sServerName      "Dedicated Together Server"  > live                    |
|  Gameplay    uDifficulty      4            > live                                    |
+--------------------------------------------------------------------------------------+
```

Panel-to-data mapping, so nobody invents an API:

| Panel | Reads | Today's availability |
|---|---|---|
| Status bar | `CoreSnapshot{state,uptimeMs,port,playerCount,frameTimeMs}` | state/port/playerCount obtainable; `uptimeMs` from `GameServer::GetUptime()` (`GameServer.cpp:1129-1139`) but **not exported**; `frameTimeMs` computed and discarded (`GameServer.cpp:560-565`) |
| TPS curve | `CoreSnapshot::tps` | **not measured today**; the WIP core derives it from tick stamps (`ServerCore.h:196-200`, `:241-244`) — measured 57.94 for a configured 60 in my run, i.e. the estimator works |
| CPU / RAM | `cpuPercent`, `memRssBytes` | **not measured**; needs `GetProcessTimes` + `GetProcessMemoryInfo` in the backend (or `/proc/self/stat` on Linux) |
| Net / packets | `netIn/OutBytesPerSec`, `packetsIn/Out` | **not measured**; needs transport counters from the `TiltedPhoques::Server` base — **TO VERIFY** (see R5) |
| Player table | `ServerCore::Players()` -> `PlayerRow` | `connectionId`, `playerId`, `username`, `isAdmin` all available (`Player.h:15-21`, `GameServer.h:90`); **`pingMs` and `connectedAtMs` are not** (see F11) |
| Console | `LogModel` + `SubmitCommand` | new; command execution already exists (`ConsoleRegistry.cpp:223-274`, `:331-343`) |
| Config | `ConsoleRegistry::ForAllSettings` -> `(name, desc, type, value)` | fully available (`ConsoleRegistry.h:76-80`, `Setting.h:114-115`, `:182`); the ini round-trip already exists (`IniSettingsProvider.cpp:63-102`, `:104-141`) |

### 6.3 Frame budget: UI never blocks > 16 ms

Target: 60 FPS, hard cap 16.6 ms per frame, and **no unbounded operation on the UI thread at
all**. The rule is structural, not a measurement: every panel's cost is bounded by screen
size, never by history size.

| Panel | Mechanism | Worst case per frame | Bound by |
|---|---|---|---|
| Status bar | 6 `TextUnformatted` + 3 `Button` from one `CoreSnapshot` copy | < 20 µs | constant |
| Metrics plots | `PlotLines` over a fixed 300-sample circular buffer (one sample per frame, 5 s window at 60 FPS) | < 60 µs for 4 plots | sample count, **not** uptime |
| Player table | `ImGuiListClipper` over the `Players()` snapshot copy; snapshot taken **once per frame**, not per row | < 80 µs for 32 visible rows | visible rows |
| Console | clipper over `LogModel`; <= 24 rows formatted (§5.6) | < 50 µs | clip height |
| Config | values read once on open/apply; drawing is a loop over `ForAllSettings` inside a clipper | < 100 µs | visible settings |
| Log drain | `Drain` + `Commit` with the `kMaxBatch` cap | < 200 µs at 4096 records | batch cap |
| Present | one `SwapChain::Present(1, 0)` | vsync | 16.6 ms |

Budget check: ~0.5 ms of CPU work + vsync. There is **~30x headroom**, which is what makes
the 16 ms promise survivable on a machine that is simultaneously running the server.

Three anti-patterns this design forbids, each with the current code as the cautionary tale:

1. **No `SendMessage` to another thread.** Use `PostMessage` for wake-ups
   (`ServerGui.cpp:130` already does) and never for content.
2. **No full-text rebuild.** `SetWindowTextW` on accumulated history (`ServerGui.cpp:182`) is
   exactly the pattern being removed.
3. **No lock held across a draw.** The UI thread takes no mutex while drawing; it reads
   atomically published snapshots (`ServerCore.h:232-234`) and the `LogModel`, both
   UI-thread-owned after `Commit`.

---

## 7 · Risk register and migration plan

### 7.1 Risk register

| # | Risk | Evidence | Impact | Mitigation |
|---|---|---|---|---|
| R1 | **No xmake on this host; nothing in this document has been built here** | `which xmake` -> not found | A design that compiles in prose and not in CI | Every dependency-free unit is verifiable now with `g++ -std=c++20 -fsyntax-only`. **I ran it**: `ServerCore.cpp`, `LaunchMode.cpp`, `ConsoleHost.cpp` are clean, and `core_snapshot_test.cpp` links and runs (tps 57.94, "all checks passed"). The GUI target is *not* verifiable here — treat §4.2 as unproven until a machine with xmake links it. |
| R2 | **Two spdlog instances** across the DLL boundary: runner is `SPDLOG_HEADER_ONLY`, DLL uses the compiled package | `server_runner/xmake.lua:30` vs `server/xmake.lua:29-38`; handover `server/main.cpp:109-122` | A sink created in one and registered in the other is UB; today it works only because the *logger* is handed over as an opaque `shared_ptr` | Create `LogSink` **inside the runner's** spdlog, inside `LogInstance` (`server_runner/main.cpp:63-81`), before `SetDefaultLogger` at `:81`. Never export a sink. Keep the existing handover shape. Follow-up: make both sides header-only or neither. |
| R3 | **Non-atomic stop flag** — `m_requestStop` is `bool` | `GameServer.h:143`, written `GameServer.cpp:234`, read `:571` | UB / lost shutdown on the current path | The new path never calls into the server from the UI thread: `ServerCore::RequestStop` sets an atomic and the core thread calls `Shutdown()`. Do not "fix" this by making the old export thread-safe and keeping it. |
| R4 | **Restart in-process is not safe today, and the real reason is not the one in the comment** — see F9 | `ConsoleRegistry.cpp:66-89` drains the global static command/setting lists into the first registry and sets `ROOT() = nullptr`; `Command.h:76-80`, `Setting.h:63-67` | A second `ConsoleRegistry` sees **zero** commands and **zero** settings; `SaveSettingsToIni` on the dtor path (`DediRunner.cpp:51-52`) then writes an **empty ini**, destroying the user's config | Do **not** implement Restart by reconstructing `DediRunner`. Either (a) restart by relaunching the process (child process / `ShellExecute`), or (b) first make the static lists re-registrable. v1: (a). |
| R5 | `TiltedPhoques::Server` base is not vendored in this checkout | `Libraries/` contains only `cxxopts.hpp`, `magic_enum.hpp`, `xmake.lua`; `GameServer.h:26` derives from `Server` and calls `IsListening/GetPort/GetTickRate/GetClientCount` (`GameServer.cpp:188`, `:1145-1147`) | `netIn/Out`, `packetsIn/Out`, and possibly `pingMs` cannot be designed precisely | **TO VERIFY** on a host with the tiltedcore package fetched. The seam is already correct: these belong behind `ICoreBackend` (`ServerCore.h:126-150`), not in the UI. Ship the dashboard with `-` for these four fields. |
| R6 | **`pingMs` and `connectedAtMs` have no data source** | Repo-wide grep for `ping/latency/rtt` in `Code/server` and `Code/common` -> **0 hits**; `Player.h:45-58` has no timestamp and no RTT field | The contract's `PlayerRow` cannot be fully populated | Render `-`. Fill `connectedAtMs` in the backend by stamping `OnConnection` (`GameServer.cpp:641-645`). `pingMs` needs either a ping/pong opcode (new protocol) or the transport's per-connection RTT — **TO VERIFY** via R5. Do not fabricate a number. |
| R7 | `LogRecord::text` lifetime misuse | frozen contract | Use-after-free, corrupt rendering | `LogModel` copies at `Commit` (§5.5). Assert in debug that the model never holds a ring pointer. |
| R8 | D3D11 device loss (TDR, driver update, RDP session change) | new code | Blank window until restart | Handle `DXGI_ERROR_DEVICE_REMOVED` on `Present`: destroy device/swapchain, recreate, re-upload the font atlas. Test with a forced TDR or by locking the workstation. |
| R9 | The `server_runner/**` auto-glob compiles anything new into the shipping binary | `server_runner/xmake.lua:13-14` | Accidentally shipping half-built UI code | Keep all new code in `Code/server_ui/`, which is not in `Code/xmake.lua:2-17`. Landing = adding one `includes("server_ui")` line plus uncommenting the target (`server_ui/xmake.lua:35-43`). |
| R10 | Config write-back races the server's own save | `SaveSettingsToIni` on `kDirty` (`DediRunner.cpp:192-193`) and in the dtor (`:51-52`); `LoadSettingsFromIni` (`IniSettingsProvider.cpp:104-141`) | Truncated/corrupt ini if the UI writes while the core saves | All config writes go through `ServerCore::ApplySettings` and execute **on the core thread** (`ServerCore.h:190`, `:203`). The UI never opens the ini for writing. |
| R11 | `ApplySettings` cannot change port/tickrate live | `uServerPort` is read once in the `GameServer` ctor (`GameServer.cpp:161-166`); tick rate is `GetUserTickRate()` (`:129-132`) consulted at `Host()` time | "Apply" silently does nothing, or lies | Mark such settings **restart required** in the config panel (the wireframe already shows a "restart required" column). `SettingsPatch` carries `std::optional` fields (`ServerCore.h:102-108`) precisely so "not asked for" and "rejected" are distinguishable. |
| R12 | Console command responses are on a **different logger** than server logs | `ConOut` created `server_runner/main.cpp:63`, used by 15 call sites, e.g. `GameServer.cpp:313` | The console pane shows either commands with no context, or context with no responses | Attach `LogSink` to **both** `ConOut` and `globalOut` (§1.2 D4). One sink instance can be shared via `sinks_init_list` (`main.cpp:71`) + `register_logger` (`server/main.cpp:116-122`). |

### 7.2 Migration plan (each step shippable and independently verifiable)

| Step | Change | Verifiable by | Ships value |
|---|---|---|---|
| **M0** | Land `LogRing` + `LogModel` in `Code/server_ui/src/log/` (contract types only, no spdlog) | `g++ -std=c++20` build of `tests/log_ring_test.cpp`: single-producer order, MP stress, drop counter, `seq` gap detection, wrap-around, `Drain`-invalidates-text | A tested, dependency-free ring. No binary changes. |
| **M1** | Land `LaunchMode` + `ConsoleHost` in `src/launch/` | `tests/launch_mode_test.cpp` (already exercises `IsGraphicalEnvironment` stability, `launch_mode_test.cpp:154-161`) plus the manual six-case matrix from §4.5 | Mode resolution is correct before any window exists. |
| **M2** | Land `ServerCore` + `ICoreBackend` with a **fake** backend | `tests/core_snapshot_test.cpp` — **already builds and passes on this host**: tps 57.94/60, concurrent `Snapshot()` 14,059,726 reads, caller != executor, dtor joins without `RequestStop` | The threading contract is proven before the DLL is involved. |
| **M3** | `LogSink` + attach it to **both** loggers in `LogInstance` | Run the *existing* console runner with `--nogui`; dump `ring.TotalCount()/DroppedCount()` at exit; confirm the count matches the file sink's line count +/- 1 | The log stream is real, with no UI yet. |
| **M4** | `DediBackend` over `DediRunner`/`IGameServerInstance`; `ServerCore` drives the real server | Start/Stop a real server from a test harness; assert `state` transitions and that `config/STServer.ini` is **not** truncated (R4) | The core owns the server. No UI yet. |
| **M5** | `GUIController`: Win32 + D3D11 + ImGui shell, **status bar + console pane only** | Launch, see live server lines, resize, close; measure frame time with a temporary overlay | First user-visible win: no black window, real log. |
| **M6** | Metrics dashboard (TPS/frame/CPU/RAM/net/packets) + fixed 300-sample ring | Frame time still <= 16.6 ms under a 4096-record burst; `-` rendered for unmeasured fields | The dashboard. |
| **M7** | Player table + context menu (Kick/Ban/DM) mapped to existing console commands; `pingMs`/`connectedAtMs` filled or `-` | Kick a real client; verify the row disappears within one frame | Admin actions in the GUI. |
| **M8** | Config panel (`ForAllSettings` + `ApplySettings`) with live / restart-required classification | Change a live setting, observe effect; change `uPort`, observe "restart required" | Hot-reload for what can be reloaded. |
| **M9** | Flip the build: add `includes("server_ui")` to `Code/xmake.lua`, uncomment the target, switch the runner to `/SUBSYSTEM:WINDOWS` | Full xmake build on a machine that has it; the six-case launch matrix from §4.5 | The redesign replaces the panel. |

Ordering rationale: M0–M2 are pure and verifiable **on this host today** (I verified M2 by
running it). M3–M4 make the data real. M5–M8 build the UI on data that already exists, so no
panel is ever written against a stub. M9 is the only step that touches the shipping binary,
and it is last.

---

## 8 · What I would NOT build

| Not building | Why |
|---|---|
| **Qt / any large widget toolkit** | 0 hits in the repo; ImGui is already vendored with the right backends; the panel is a table, a few plots and a text view. |
| **A second process for the UI (IPC / local socket)** | `ServerCore` already gives a thread-safe facade in-process. IPC buys crash isolation nobody asked for and costs a protocol, serialization and a lifecycle. |
| **Docking / multi-viewport / detachable panels** | ImGui v1.89.7's docking lives on a separate branch; nothing here needs rearrangeable panels. Fixed layout, one window. |
| **A plotting library** | `ImGui::PlotLines` over a fixed circular buffer is ~20 lines. |
| **A custom command parser for the GUI console** | `ConsoleRegistry::TryExecuteCommand` already exists with typed args, error messages and history (`ConsoleRegistry.cpp:223-274`). The GUI is a `SubmitCommand` producer, nothing more. |
| **A per-field config UI** | `ForAllSettings` + `SettingBase::name/desc/type` is a generic loop (`ConsoleRegistry.h:76-80`, `Setting.h:114-115`). |
| **A new log file format / structured JSON logs** | spdlog's rotating file sink already works (`server_runner/main.cpp:69`). The ring is for display, not storage. |
| **Log persistence beyond the ring (search, export, "save log as…")** | The file sink is the archive. Say so in the UI footer. |
| **Timestamp seek / jump-to-time in the log view** | Follow-tail is O(1) and is what operators use. Binary search over timestamps is O(log n) to add later. |
| **"Restart in-process" by reconstructing `DediRunner`** | It silently empties the command/setting registry and can truncate the ini (R4 / F9). Relaunch the process. |
| **A ban database with its own schema** | Not requested; there is no persistence layer for it today. Kick is a command; ban can be a text file when someone asks. |
| **Daemonisation / double-fork / a Windows service wrapper on Linux** | systemd `Type=simple` is the deployment story. |
| **Theming engine, accent-colour picker, light mode** | One dark palette (`ImGuiDriver.cpp:14-30` as the starting point). "Modern dark" is one style block, not a feature. |
| **Player avatars / map / in-GUI world view** | Out of scope for a control panel; the game client is where a world view belongs. |
| **Auto-update** | Orthogonal to this document, and a supply-chain decision. |
| **Fixing `AdminService`'s admin-log protocol so the GUI can reuse it** | It is deliberately inert (`AdminService.cpp:31-50` explains why the frame is not built at all: two protocols share a numbering, not a layout). The ring is the right transport. |
| **Wiring the dead EULA gate (`main.cpp:146-196`)** | Not this project. Flagged as D10 so it is not forgotten. |

---

## Appendix A · Evidence index

Files read in full or in the cited ranges (paths relative to the repo root):

`Code/server_runner/main.cpp` (245), `DediRunner.h` (60), `DediRunner.cpp` (194),
`gui/ServerGui.cpp` (325), `gui/ServerGui.h` (21), `xmake.lua` (41), `server_runner.rc` (34),
`resources/server.manifest` (38), `Code/server/main.cpp` (139), `Code/server/GameServer.h` (146),
`Code/server/GameServer.cpp` (ranges 20-64, 125-144, 145-244, 306-500, 545-585, 638-680,
1125-1154), `Code/server/Game/PlayerManager.h` (64), `Code/server/Game/PlayerManager.cpp`
(ranges 1-35, 140-144), `Code/server/Game/Player.h` (59), `Code/server/Services/AdminService.h`
(29), `Code/server/Services/AdminService.cpp` (1-50), `Code/server/World.cpp` (1-40),
`Code/common/GameServerInstance.h` (17), `Code/components/console/ConsoleRegistry.h` (103),
`Code/components/console/ConsoleRegistry.cpp` (1-90, 215-343),
`Code/components/console/CommandQueue.h` (50), `Code/components/console/Command.h` (30-99),
`Code/components/console/Setting.h` (45-79), `Code/components/console/IniSettingsProvider.cpp`
(60-141), `Code/base/containers/fast_queue.hpp` (147), `Code/base/Check.h` (1-45),
`Code/admin/AdminApp.cpp` (1-60), `Code/admin/AdminApp.h`, `Code/admin/Widgets/Console.cpp`
(148), `Code/admin/xmake.lua` (31), `Code/components/imgui/ImGuiDriver.cpp` (1-30, 105-126),
`Code/components/imgui/ImGuiDriver.h` (18), `Code/components/imgui/xmake.lua` (5),
`Code/components/xmake.lua` (1-55), `Code/components/console/xmake.lua` (2),
`Code/server/xmake.lua` (45), `Code/immersive_launcher/xmake.lua` (66),
`Code/xmake.lua` (17), `xmake.lua` (1-82), `Code/server_ui/README.md` (40),
`Code/server_ui/xmake.lua` (43), `Code/server_ui/src/core/ServerCore.h` (255),
`Code/server_ui/src/launch/LaunchMode.h` (44), `Code/server_ui/src/launch/ConsoleHost.h` (38).

Commands run to verify claims in this document:

```
which xmake                    -> not found (R1)
g++ --version                  -> 16.1.0
clang++ --version              -> 21.1.8 (x86_64-pc-windows-msvc); no MSVC STL: <array> not found
g++ -std=c++20 -fsyntax-only -I Code/server_ui/src \
    Code/server_ui/src/core/ServerCore.cpp \
    Code/server_ui/src/launch/LaunchMode.cpp \
    Code/server_ui/src/launch/ConsoleHost.cpp        -> clean, no diagnostics
g++ -std=c++20 -O1 -I Code/server_ui/src -o coretest \
    Code/server_ui/tests/core_snapshot_test.cpp \
    Code/server_ui/src/core/ServerCore.cpp           -> links
./coretest                                           -> all checks passed
    measured tps = 57.94 (configured tickRate 60)
    concurrent Snapshot() reads = 14059726
    caller tid != executing tid: yes
    destructor joined without an explicit RequestStop: ok
grep -rn "SUBSYSTEM"                                 -> immersive_launcher:16, server_ui:40 (commented)
grep -rn "Qt6|Qt5|QApplication|find_package(Qt"      -> 0 hits
grep -rn "AppendLogLine"                             -> 5 hits, all literals (ServerGui.cpp)
grep -rn "AttachConsole|AllocConsole|FreeConsole|freopen_s" -> 0 hits (not implemented today)
grep -rn "ping|latency|rtt" Code/server Code/common   -> 0 relevant hits (R6)
grep -rn "IsEULAAccepted"                            -> 1 hit, the definition only (D10)
grep -rn "spdlog::get(\"ConOut\")"                   -> 15 call sites
```

## Appendix B · The eight findings, adjudicated

| # | Claim | Verdict | What I actually checked |
|---|---|---|---|
| 1 | The feature partly exists: `gui/ServerGui.cpp` is a 325-line pure-Win32 panel that already runs the server on a background thread | **CONFIRMED** | File is 325 lines (`ServerGui.cpp:1-325`); dependency-free claim `:1-6`; thread at `:118-132`; server loop `DediRunner.cpp:138-150`; compiled only on Windows `server_runner/xmake.lua:17-19`. |
| 2 | `PumpLog()` at `:182` calls `SetWindowTextW` on the entire accumulated log every 500 ms — full rebuild + a synchronous `SendMessage` per tick | **CONFIRMED** | `ServerGui.cpp:171-187`; rebuild loop `:176-181`, `SetWindowTextW` `:182`, two `SendMessageW` `:185-186`; 500 ms at `:59-60` and `:307`; handler `:230-233`. Nuance: the rebuild happens **inside** the timer handler, so the freeze is on the UI thread for the duration of rebuild + re-layout. |
| 3 | `AppendLogLine` (`:88-97`) takes a `std::mutex` per line; the deque is capped at 400 by silently popping the front | **CONFIRMED** | `ServerGui.cpp:88-97`: `scoped_lock` `:93`, `push_back` `:94`, `while(size>400) pop_front()` `:95-96`; member `:79-80`. |
| 4 | The GUI log is not fed by the real log stream; `AppendLogLine` is only called with literals; real output goes to `stdout_color_sink` + `rotating_file_sink` only (`main.cpp:63-81`) | **CONFIRMED, with one correction** | Five call sites, all literals: `:119`, `:122`, `:129`, `:140` (+ definition `:88`). Sinks: `server_runner/main.cpp:63` (ConOut), `:69-71` (file + stdout on `globalOut`). **Correction/addition:** a **third** sink is registered at runtime — `World.cpp:24-25` pushes `AdminService` onto the default logger — but its `sink_it_` deliberately does nothing (`AdminService.cpp:31-50`). So "only stdout + file" is true in effect. The more important addition is D4: `ConOut` is a **separate logger**, so a sink on the default logger alone would still miss all console-command output. |
| 5 | The runner is a console-subsystem binary; there is no `/SUBSYSTEM:WINDOWS` anywhere except `immersive_launcher` | **CONFIRMED, with one nuance** | Repo-wide grep for `SUBSYSTEM` -> `immersive_launcher/xmake.lua:16` (real) and `server_ui/xmake.lua:40` (**commented out**). `server_runner/xmake.lua:3-31` sets no subsystem -> default `CONSOLE`. |
| 6 | The runner cannot be restarted in-process; `ServerGui.cpp:109-115` says so, so Start only works once | **CONFIRMED, and the stated reason is incomplete** | Guard at `ServerGui.cpp:106-115` (`startRequested.exchange(true)`), never reset; button disabled `:167`; UI text `:160-161`. **The real mechanism (F9):** `ConsoleRegistry::BindStaticItems` drains the global static `CommandBase::ROOT()`/`SettingBase::ROOT()` linked lists into the first registry and sets both to null (`ConsoleRegistry.cpp:66-89`, roots at `Command.h:76-80`, `Setting.h:63-67`). All server settings are static objects registered at load time (e.g. `GameServer.cpp:30-52`, `DediRunner.cpp:23`). A second registry therefore gets **zero commands and zero settings**, and the dtor's `SaveSettingsToIni` (`DediRunner.cpp:51-52`) would overwrite `config/STServer.ini` with an empty file. The comment's "cannot be constructed twice" is right; the consequence is worse than "Start only works once". |
| 7 | Telemetry is a single function `QueryServerPlayerCount()` (`server/main.cpp:77`); no TPS/frame/CPU/memory/bandwidth export | **CONFIRMED** | `server/main.cpp:75-90` exports exactly two telemetry functions (player count, shutdown request). No other `GS_EXPORT` carries a metric (`:65-122`). `GameServer::OnUpdate` computes `cDelta` and discards the value after building the event (`GameServer.cpp:560-565`); `GetUptime()` exists (`:1129-1139`) but is not exported. |
| 8 | ImGui v1.89.7 is already an `add_requires` package on Windows, `Code/admin` already ships a working ImGui app, and Qt6 appears nowhere | **CONFIRMED, with one caveat** | `xmake.lua:53-58` (`imgui v1.89.7`, Windows only); `AdminApp.cpp:12` constructs `ImGuiIntegration::Context`, `AdminApp.h:3` includes it; Qt grep -> **0 hits**. Caveat: whether `Admin` *builds* today is **TO VERIFY** — `AdminApp.h:5` needs `Magnum/Platform/Sdl2Application.h`, the magnum imgui/sdl2 `add_requireconfs` are commented out (`xmake.lua:69-74`), and `Code/admin/xmake.lua:24-31` lists neither `sdl2` nor `imgui` directly. The stronger, self-contained prior art is `Code/components/imgui/` — vendored `imgui_impl_win32.cpp` (670 lines) + `imgui_impl_dx11.cpp` (687 lines) + a dark style with DPI scaling (`ImGuiDriver.cpp:14-30`, `:112-126`) — exactly the pair a Win32 host needs, and it needs nothing from Magnum. |

### Additional findings that change the design (not on your list)

| # | Finding | Evidence | Why it changes the design |
|---|---|---|---|
| F9 | **A second `ConsoleRegistry` is empty and can wipe the ini** — see finding 6 | `ConsoleRegistry.cpp:66-89`, `Command.h:76-80`, `Setting.h:63-67`, `DediRunner.cpp:51-52`, `GameServer.cpp:30-52` | Kills "restart = reconstruct `DediRunner`". Restart must relaunch the process (R4, M4). |
| F10 | **Three live data races** on the current stop/telemetry path: non-atomic `m_requestStop`, unlocked `PlayerManager::Count()` from the UI thread, raw `s_pInstance` read from the UI thread | `GameServer.h:143` / `GameServer.cpp:234` / `:571`; `ServerGui.cpp:164` -> `main.cpp:83` -> `PlayerManager.cpp:141-144`; `GameServer.h:145` / `main.cpp:79` | The current panel is not merely slow, it is UB. Reinforces "the UI never calls into server state" as a hard rule, not a preference. |
| F11 | **`pingMs` and `connectedAtMs` have no data source in the repo** | 0 grep hits for ping/latency/rtt in `Code/server` / `Code/common`; `Player.h:45-58` has no timestamp/RTT field | The contract's `PlayerRow` is only half-fillable today. Render `-`; do not fabricate (R6). |
| F12 | **Console responses live on `ConOut`, a different logger from server logs** | `server_runner/main.cpp:63`; 15 `spdlog::get("ConOut")` sites, e.g. `GameServer.cpp:313,320,339,358,375,397` | The `LogSink` must be attached to **both** loggers, or the console pane shows commands with no answers (R12). |
| F13 | **The GUI is chosen by default with no environment probe**, and a failed window creation just `return 1` | `ServerGui.cpp:268-281` (default `true`), `:299-303` | A service or Session-0 run gets a silent exit code 1 instead of falling back to CLI. `ResolveLaunchMode` must probe the window station (§4.5, `LaunchMode.cpp:137`, `:216`). |
| F14 | **Ctrl-C handling is disabled**, so the only shutdown path is the GUI's | `server_runner/main.cpp:229-230` | `EnsureConsoleForMode` must (re)install `SetConsoleCtrlHandler` after a console exists; otherwise a CLI user's Ctrl-C is not a graceful stop. |
| F15 | **`SaveSettingsToIni` runs on `kDirty` from the command path and in the dtor** | `DediRunner.cpp:192-193`, `:51-52`; `IniSettingsProvider.cpp:63-102` | The GUI must never write the ini directly; all writes go through the core thread (R10). |
| F16 | **The manifest already declares PerMonitorV2** and is embedded by the runner's `.rc` | `server_runner/resources/server.manifest:18-19`, `server_runner.rc:32` | Reuse it for the new target; do not add a second manifest (DPI bugs are a common ImGui-on-Win32 failure). |
| F17 | **`BASE_ASSERT` does not abort** — it logs `critical` and `__debugbreak` only under a debugger | `base/Check.h:9-24` | The single-instance assertion in `GameServer` (`GameServer.cpp:158`) will **not** stop a second server from overwriting `s_pInstance` in a release build. Do not rely on it as a restart guard. |
| F18 | **The WIP `server_ui` core already works on this host** | `core_snapshot_test.cpp` built with g++ 16.1.0 and ran: tps 57.94/60, 14,059,726 concurrent `Snapshot()` reads, clean dtor join | M0–M2 are not speculative; the threading design in §2.3 is already demonstrated. Caveat: this tree is being written by another stream in parallel and may change under me. |

---

*End of document.*
