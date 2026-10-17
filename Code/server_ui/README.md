# ServerUI — dual-mode (CLI / GUI) dedicated server, proof of concept

Isolated redesign tree. **Nothing here is part of the shipping build**: `Code/xmake.lua`
includes a fixed list of subdirectories and `server_ui` is not one of them.

## Why a separate tree

`Code/server_runner/xmake.lua` uses `add_files("**.cpp|gui/**.cpp")`. Any new `.cpp`
dropped under `Code/server_runner/` is compiled straight into the shipping runner, so the
existing Win32 panel cannot be redesigned in place without risking the live server binary.

## Layout

| Path | Contents |
| --- | --- |
| `src/log/` | `LogRing` (lock-free MP/SC ring), `LogSink` (spdlog adapter), `LogModel` (virtualized view model) |
| `src/launch/` | `LaunchMode` (CLI/GUI/headless routing), `ConsoleHost` (no-black-window console control) |
| `src/core/` | `ServerCore` facade + `ICoreBackend` seam to `STServer.dll` |
| `tests/` | Standalone executables, plain asserts, no gtest dependency |

## Verifying without xmake

xmake is not installed on every dev host. The core pieces are dependency-free and can be
checked directly:

```sh
clang++ -std=c++20 -O2 -Wall -Wextra -Werror -fsyntax-only src/log/LogRing.cpp
clang++ -std=c++20 -O2 -Wall -Wextra -Werror -o /tmp/logring tests/log_ring_test.cpp src/log/LogRing.cpp src/log/LogModel.cpp
/tmp/logring
```

## Opting into the real build

Add to `Code/xmake.lua`:

```lua
includes("server_ui")
```

and uncomment the `SkyrimTogetherServerUi` target in `xmake.lua` here.
