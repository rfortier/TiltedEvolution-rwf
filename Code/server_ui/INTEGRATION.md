# Integration notes — wiring server_ui into the real runner

Status: **wired on Windows, but never compiled end to end.** The runner now references
`Code/server_ui/` behind `SERVERUI_ENABLED`, and `main()` routes through
`ResolveLaunchMode` / `EnsureConsoleForMode`. What that wiring has and has not been
verified is listed below; read it before trusting a build.

## 0. What was actually verified

| Piece | How it was checked |
| --- | --- |
| `Player::connectedAtMs` / `pingMs` | **Real `Player.cpp` compiled** against stub headers, then run: 6 assertions (stamped, default 0, setter, move carries both, move source reset) |
| `QueryServerPlayers` export | **Real function body compiled and run** against a stub `GameServer`: 8 assertions incl. capacity clamp, null buffer, no-server |
| `DediCoreBackend.cpp` | Compiled against stub `ServerCore`/`DediRunner`, `-Wall -Wextra -Werror` clean |
| `gui/ServerGui.cpp` (incl. new `RunGuiWithCore`) | Compiled against stub Win32 + stub `ServerCore`, clean |
| `DediRunner::TickOnce` | Read-only review; the body is the old loop body verbatim |
| `main.cpp` mode routing | **NOT compiled** — depends on spdlog, uv, TiltedCore, SimpleIni, none present here |
| Whole runner link | **NOT attempted** — no xmake, no `STServer.dll` |
| `imgui.h` draw layer | **NOT compiled** — the package is absent |

Every claim of "compiled" above used stub headers for the project's own dependencies. That
catches syntax and type errors in the code written here; it does **not** prove the real build
links or behaves identically.

## 0.1 Deliberate design points in the wiring

- **One tick loop, not two.** `DediRunner::RunGSThread()` was split into `TickOnce()` so
  `ServerCore` can drive it. `DediCoreBackend::Update()` calls `TickOnce()`;
  `IsListening()` is a pure query and must never tick, or every status poll would advance the
  simulation.
- **One server per process.** `RunGuiWithCore()` reads an externally owned `ServerCore` and
  never constructs a `DediRunner`; the legacy `RunGui()` still owns its own runner for builds
  without `SERVERUI_ENABLED`. Using both at once would put two servers on one port.
- **The dashboard path uses the classic Win32 panel**, not the ImGui one. `RunGuiWithCore` is
  a new window in `ServerGui.cpp` driven by `ServerCore::Snapshot()`. The ImGui dashboard
  (`src/ui/DashboardImGui.cpp`) is compiled into no target yet.
- **`--generate-config` and settings failures** land in the `!IsValid()` branch and exit
  without opening a window.

## 1. Build

Add to `Code/xmake.lua`:

```lua
includes("server_ui")
```

The shipping runner (`Code/server_runner/xmake.lua`) must then gain the windowed subsystem:

```lua
if is_plat("windows") then
    add_ldflags("/SUBSYSTEM:WINDOWS", "/ENTRY:mainCRTStartup", { force = true })
end
```

`/ENTRY:mainCRTStartup` is **required**: `/SUBSYSTEM:WINDOWS` otherwise defaults the entry
point to `WinMainCRTStartup`, and this codebase has a plain `int main(int, char**)`.
Without it the link fails on an unresolved `WinMain`.

Verified on this host with MinGW: a windows-subsystem binary reports
`GetConsoleWindow() == NULL`; a console-subsystem binary reports a live console.

## 2. Log wiring — the part that is currently missing

The dashboard log panel is empty today because nothing feeds the ring. Two sinks are needed,
and attaching to only one silently loses half the output:

| Logger | Created at | Carries | Must attach |
| --- | --- | --- | --- |
| `ConOut` | `server_runner/main.cpp:63` | **all console command responses** (14 call sites in `GameServer.cpp`) | yes |
| `globalOut` (default) | `server_runner/main.cpp:71` | server/game log lines | yes |

```cpp
auto pRing = std::make_shared<serverui::LogRing>(1u << 20);
spdlog::get("ConOut")->sinks().push_back(std::make_shared<serverui::LogSink>(*pRing));
spdlog::set_default_logger(...); // then push the same sink onto the default logger
```

**Payload caveat.** `LogSink::sink_it_` forwards `acMsg.payload` only when it is non-empty.
spdlog fills `payload` when a pattern/formatter is applied; a logger that has never had
`set_pattern` called may hand over an empty payload and the record would be dropped silently.
Confirm at integration time that both loggers have a pattern set — `main.cpp:64` and
`main.cpp:72` both do, but a newly created logger elsewhere would not.

**Lifetime.** The ring must outlive both loggers. `main.cpp`'s `LogInstance` is a stack
object in `main` and `spdlog::shutdown()` runs in its destructor, so the ring must be
declared **before** `LogInstance` (destroyed after it).

## 3. Drain loop

`Drain()` returns at most `SlotCount()` records per call regardless of `aMaxRecords`
(the scratch arena holds exactly that many). Callers must **loop** until it returns 0;
assuming one call empties the ring loses records. At the default 1 MiB ring that is 2114
records per call.

## 4. Console control handlers

`RegisterQuitHandler()` is commented out in `server_runner/main.cpp:229-230`. Once the
binary is `/SUBSYSTEM:WINDOWS` and `ConsoleHost` has attached or allocated a console,
`SetConsoleCtrlHandler` must be installed **after** the console exists, or Ctrl-C /
window-close will terminate the process without a graceful shutdown.

## 5. Restart

**Restart must relaunch the process.** `ConsoleRegistry::BindStaticItems` drains the global
static command/setting roots and nulls them (`ConsoleRegistry.cpp:70-71`, `:80-81`), so a
second `ConsoleRegistry` receives zero commands and zero settings, and its destructor's
`SaveSettingsToIni` would then write an **empty** `config/STServer.ini`.

## 6. Moderation seam

`src/players/IServerModeration.h` defines what the real server must implement. Mapping:

| Dashboard action | Real server call | Status |
| --- | --- | --- |
| Kick | close the connection for that `ConnectionId_t` | **API not yet identified** — `TiltedPhoques::Server` is a tiltedcore package, not vendored |
| Ban | same close, plus a `BanList` entry persisted to disk | close API unknown; `BanList` is implemented and tested |
| Direct message | `Player::Send(NotifyChatMessageBroadcast)` | available today, no new wire protocol |
| Player IP | `Player::GetEndPoint()` (`Player.h:21`) | available today |
| Player ping | **no data source** — `Player.h` has no RTT field | must be added server-side |
| Connected duration | **no data source** — `Player.h` has no timestamp field | must be added server-side |

## 7. Known-unverifiable items on this host

- `xmake` is not installed: no target in this repo was built with it.
- `imgui.h` is an xmake package, not vendored: `src/ui/DashboardImGui.cpp` is **not**
  compile-verified. Only `DashboardModel`/`Sparkline` (pure) are.
- No sanitizers in w64devkit: concurrency rests on memory-ordering reasoning and stress runs,
  not on TSAN/ASAN.
- Nothing links the real `STServer.dll`; `ICoreBackend` is the seam.
