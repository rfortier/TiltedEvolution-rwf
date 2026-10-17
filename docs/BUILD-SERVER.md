# Building the dedicated server

Two supported platforms, one source tree. The whole build is driven by **xmake**
(`xmake.lua` at the repository root). There is no CMake, no MSBuild solution to
maintain by hand.

| Platform | Status | Entry |
| --- | --- | --- |
| Windows 10 / 11 (x64, MSVC) | supported | `xmake` or `.github/workflows/windows.yml` |
| Ubuntu 22.04 / 24.04, Debian 12 (x64, GCC) | supported | `xmake` or `Dockerfile` |

---

## 1. What differs per platform

The server is one binary plus one shared library on both platforms:

| Target | Windows | Linux |
| --- | --- | --- |
| `SkyrimTogetherServer` (shared) | `STServer.dll` | `libSTServer.so` |
| `SkyrimServerRunner` (binary) | `SkyrimTogetherServer.exe` | `SkyrimTogetherServer` |

Shared by both: the log capture pipeline, mode routing, player registry, ban
list, moderation logic and the metrics sampler.

Platform-specific, and the reason the build has `if is_plat(...)` branches:

| Concern | Windows | Linux |
| --- | --- | --- |
| GUI | ImGui + D3D11 dashboard (vendored backends) | ImGui + **GLFW/OpenGL3** dashboard |
| Shared draw layer | `ui/DashboardImGui.cpp` | the **same** `ui/DashboardImGui.cpp` |
| Console handling | `AttachConsole`/`AllocConsole`/`FreeConsole` | not applicable; the terminal owns stdio |
| CPU / RSS metrics | `GetProcessTimes`, `GetProcessMemoryInfo` | `/proc/self/stat`, `/proc/self/statm` |
| Network counters | `GetIfTable2` (system-wide) | `/proc/net/dev` (system-wide) |
| Resources / subsystem | `.rc`, `/SUBSYSTEM:WINDOWS` | linker flags only |

Both platforms share one launch rule: **GUI first**, decided by a single
environment probe **at startup**. `IsGraphicalEnvironment()` answers false with
no `DISPLAY`/`WAYLAND_DISPLAY`, so a headless VPS runs as a console server
without any configuration.

### Mode matrix

| Situation | Result |
| --- | --- |
| No arguments, display present | **GUI** |
| No arguments, no display (VPS, CI, `ssh` without X) | CLI, chosen by the startup probe |
| `--gui` | GUI, even with no display |
| `--cli` / `--console` / `--nogui` | CLI, always |
| `--headless` | no console interaction, file logging only |
| config key `launch/mode=` | overrides the probe, loses to the flags above |

### There is no runtime fallback

The probe decides once, at startup. It can answer "a display exists" while the
window then fails to open — broken X forwarding, a driver refusing a GL context,
a compositor refusing a surface. In that case the process **reports the failure
and exits non-zero**; it does not quietly become a console server.

That is deliberate: a deployment monitoring for a GUI should not see a healthy
process running in a mode nobody selected. On a host where the window cannot
open, start with `--cli` or `--headless`.

Note this supersedes the earlier "no display → automatically downgrade at
runtime" behaviour: the downgrade now happens only in the startup probe.

CPU/RSS/network metrics are real on both platforms now. One known asymmetry:
`/proc/net/dev` cannot identify tunnel adapters, so a VPN's virtual interface
**is** counted on Linux, while the Windows implementation excludes
`IF_TYPE_TUNNEL`.

## 2. Ubuntu / Debian build

### Dependencies

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential curl pkg-config git ca-certificates unzip libssl-dev
```

The GUI additionally needs the window-system and GL development headers, because
xmake builds GLFW and Dear ImGui from source against them:

```sh
sudo apt-get install -y --no-install-recommends \
    libgl1-mesa-dev xorg-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev
```

| Package | Why |
| --- | --- |
| `libgl1-mesa-dev` | GL headers and libGL for the OpenGL3 backend |
| `xorg-dev` | umbrella for the X11 headers GLFW needs on a default Ubuntu |
| `libxrandr-dev` | GLFW's monitor/video-mode code |
| `libxinerama-dev` | GLFW multi-monitor placement |
| `libxcursor-dev` | GLFW cursor shapes (ImGui sets the text/arrow cursor) |
| `libxi-dev` | XInput2, which GLFW uses for raw input events |
| `libwayland-dev`, `libxkbcommon-dev` | Wayland sessions; a Wayland desktop without these gets an X11 fallback only |

A **runtime** container needs the non-`-dev` equivalents:
`libgl1 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6`. A headless
server container needs none of them — it never opens a window.

The project pins its own third-party packages through xmake
(`tiltedcore`, `spdlog`, `gamenetworkingsockets`, `lua`, `sol2`, `entt`,
`glm`, `sqlite3`, ...) so they are **downloaded on first build**, not apt-installed.

### Install xmake

```sh
curl -fsSL https://xmake.io/shget.text | bash
source ~/.xmake/profile
```

### Build

```sh
xmake config -y -m release
xmake -y
xmake install -y -o package
```

Result:

```
package/bin/SkyrimTogetherServer
package/lib/libSTServer.so
```

### Run

```sh
./package/bin/SkyrimTogetherServer --cli
```

`--cli` is the default on Linux, so it can be omitted. `--headless` runs with
file logging only. `--gui` is accepted but has no effect off Windows: the mode
router never selects it there.

### Docker (reproducible, what the release uses)

```sh
docker build -t st-server .
docker run --rm -p 10578:10578/udp st-server
```

`Dockerfile` builds on `debian:12` and ships a `debian:12-slim` runtime layer.
To collect the artifacts instead of running them, see `MakeLinux.cmd`.

## 3. Windows build

```bat
xmake config -y -m release --toolchain=msvc
xmake -y
```

Requires Visual Studio 2022 with the C++ desktop workload. The runner is linked
`/SUBSYSTEM:WINDOWS` so a GUI launch never shows a console window; `--cli`
attaches to the parent console instead.

## 4. What "both platforms" does and does not mean today

Being precise, because the Linux target is newer than the Windows one:

**Compiles on Linux** — the portable core (`Code/server_ui/src/log`,
`launch`, `core`, `players`, `ui/DashboardModel`, `ui/Sparkline`,
`metrics/*`), `DediCoreBackend.cpp`, `RunnerLaunch.cpp`,
`DashboardHostLinux.cpp`, the shared `ui/DashboardImGui.cpp`, and the whole
existing server (`Code/server`, unchanged by the UI work).

**Excluded from Linux on purpose** — `DashboardHost.cpp` (D3D11) and
`gui/ServerGui.cpp` (Win32). Each is wrapped in `#ifdef _WIN32` in addition to
being excluded from the Linux source list, so a stray glob cannot pull either in.

**Not verified on this host** — this is the important caveat. The Linux GUI was
written on a Windows host with no Linux toolchain, no Docker and no WSL
distribution, so:

| Claim | Status |
| --- | --- |
| Launch rule (GUI default, probe-driven CLI) | **Verified** — `launch_mode_test` runs |
| `DashboardHostLinux.cpp` compiles | **Only against stub GLFW/ImGui headers** |
| GLFW/OpenGL3 actually initialises | **Never executed** |
| `/proc` parsers | **Verified** by `linux_proc_test` with real captured content |
| `/proc` actually read at runtime | **Never executed** |
| xmake resolves the packages | **Never executed** |

The CI job `.github/workflows/server-linux.yml` is what closes these gaps: it
builds for real, asserts the artifacts exist, asserts no Windows-only symbol was
linked in, asserts the Linux host **is** present, and runs two smoke tests with
`DISPLAY` unset: one proving the startup probe selects CLI, one proving an
explicit `--cli` works. Until that job is green on a real Ubuntu runner, treat
the Linux GUI as unproven.
