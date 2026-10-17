local function istable(t) return type(t) == 'table' end

local function build_runner()
    set_kind("binary")
    set_group("Server")
    set_symbols("debug", "hidden")
    add_includedirs(
        ".",
        "../",
        "../server/",
        "../../Libraries/")
    add_headerfiles("**.h")
    -- Everything except the GUI subdirectory, which each platform adds
    -- explicitly. The three launch-layer files below are excluded here and added
    -- by the target, because DashboardHost.cpp includes <d3d11.h> and must never
    -- reach a Linux compile:
    --   DediCoreBackend.cpp  portable, added by the target on both platforms
    --   RunnerLaunch.cpp     portable, added by the target on both platforms
    --   DashboardHost.cpp    Windows + D3D11 only, added inside is_plat("windows")
    add_files(
        "**.cpp|gui/**.cpp|DashboardHost.cpp|DashboardHostLinux.cpp|DediCoreBackend.cpp|RunnerLaunch.cpp")
    if is_plat("windows") then
        add_files("server_runner.rc")
        -- windowed control panel (pure win32, no extra deps)
        add_files("gui/ServerGui.cpp")
        add_syslinks("gdi32")
    end
    add_deps(
        "CommonLib",
        "Console",
        "BaseLib")
    add_packages(
        "tiltedcore",
        "spdlog",
        "hopscotch-map",
        "libuv")
    add_defines("SPDLOG_HEADER_ONLY")
end

target("SkyrimServerRunner")
    set_basename("SkyrimTogetherServer")
    add_defines("TARGET_PREFIX=\"st\"")
    -- we want uniform names for both runner and dll under windows for
    -- scripting/ux reasons
    build_runner()
    -- core dll
    add_deps("SkyrimTogetherServer")

    -- Dual-mode launch layer. The core (log capture, mode routing, player list,
    -- moderation, metrics) is platform independent and is built on BOTH Windows
    -- and Linux. Only the presentation differs:
    --
    --   Windows  ImGui + D3D11 dashboard via the vendored backends (ImGuiImpl).
    --   Linux    the SAME draw layer (ui/DashboardImGui.cpp), hosted by
    --            GLFW + OpenGL3 instead of Win32 + D3D11.
    --
    -- Both platforms default to the GUI when a display is reachable and fall
    -- back to the console when it is not, so a headless VPS needs no
    -- configuration. Either way there is exactly one ServerCore and one server
    -- per process: DashboardHostLinux.cpp and DashboardHost.cpp are mutually
    -- exclusive, and the shared draw layer is added once per platform.
    add_defines("SERVERUI_ENABLED=1")
    add_includedirs("../server_ui/src")
    add_files(
        "../server_ui/src/log/**.cpp",
        "../server_ui/src/launch/**.cpp",
        "../server_ui/src/core/**.cpp",
        "../server_ui/src/metrics/**.cpp",
        "../server_ui/src/players/**.cpp",
        "../server_ui/src/ui/DashboardModel.cpp",
        "../server_ui/src/ui/Sparkline.cpp",
        "DediCoreBackend.cpp",
        "RunnerLaunch.cpp")

    if is_plat("windows") then
        add_syslinks("psapi", "iphlpapi")

        -- The ImGui dashboard. ImGuiImpl already compiles the vendored Win32 and
        -- DX11 backends (Code/components/imgui), so they are a dependency here,
        -- never a second copy of the same sources: adding those .cpp files again
        -- would duplicate ImGui_ImplWin32_*/ImGui_ImplDX11_* at link time.
        add_deps("ImGuiImpl")
        add_defines("SERVERUI_DASHBOARD_IMGUI=1")
        add_includedirs("../components/imgui")
        add_files(
            "../server_ui/src/ui/DashboardImGui.cpp",
            "DashboardHost.cpp")
        add_syslinks("d3d11", "dxgi", "d3dcompiler")
    end

    -- Linux dashboard: the same draw layer, hosted by GLFW + OpenGL3 instead of
    -- Win32 + D3D11. DashboardImGui.cpp is shared - it only ever calls ImGui
    -- core API, so it needs no per-platform variant.
    if is_plat("linux") then
        add_defines("SERVERUI_DASHBOARD_IMGUI=1")
        add_files(
            "../server_ui/src/ui/DashboardImGui.cpp",
            "DashboardHostLinux.cpp")
        add_packages("imgui", "glfw", "opengl")
    end
