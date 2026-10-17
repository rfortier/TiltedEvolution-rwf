-- ServerUI proof-of-concept build.
--
-- DELIBERATELY NOT REFERENCED BY Code/xmake.lua.
-- Code/xmake.lua includes a fixed list of subdirectories; "server_ui" is not
-- in it, so nothing here enters the shipping build. To build these targets the
-- maintainer opts in by adding the include line documented in README.md.
--
-- Why a separate tree instead of extending Code/server_runner/gui:
-- Code/server_runner/xmake.lua uses add_files("**.cpp|gui/**.cpp"), so ANY new
-- .cpp placed under Code/server_runner/ is compiled into the shipping runner.
-- A redesign cannot be landed there without risking the live server binary.

-- Every pure source file. DashboardImGui.cpp is excluded: it is the only file
-- that includes <imgui.h>, which is an xmake package rather than vendored code,
-- so it must not be pulled into the headless/verifiable targets.
local kPureSources = {
    "src/log/**.cpp",
    "src/launch/**.cpp",
    "src/core/**.cpp",
    "src/metrics/**.cpp",
    "src/players/**.cpp",
    "src/ui/DashboardModel.cpp",
    "src/ui/Sparkline.cpp",
}

-- Each test file has its own main(), so each gets its own binary. Globbing
-- tests/**.cpp into one target would collide on main().
local kTests = {
    "log_ring_test",
    "log_model_test",
    "launch_mode_test",
    "core_snapshot_test",
    "players_test",
    "metrics_test",
    "dashboard_model_test",
    "sparkline_test",
    "linux_proc_test",
}

for _, name in ipairs(kTests) do
    target(name)
        set_kind("binary")
        set_group("ServerUI")
        set_languages("cxx20")
        add_includedirs("src")
        add_files(kPureSources)
        add_files("tests/" .. name .. ".cpp")
        if is_plat("windows") then
            add_syslinks("kernel32", "user32", "psapi", "iphlpapi")
        end
end

-- The real dashboard. Needs the imgui package, so it only builds where that
-- package resolves; it is not part of the verifiable core above.
target("ServerUiDashboard")
    set_kind("binary")
    set_group("ServerUI")
    set_languages("cxx20")
    add_includedirs("src", "../components/imgui")
    add_files(kPureSources)
    add_files("src/ui/DashboardImGui.cpp")
    add_files("../components/imgui/imgui_impl_win32.cpp")
    add_files("../components/imgui/imgui_impl_dx11.cpp")
    add_packages("imgui")
    if is_plat("windows") then
        add_syslinks("kernel32", "user32", "gdi32", "d3d11", "dxgi", "d3dcompiler")
        -- no console window in GUI mode; ConsoleHost allocates one on demand.
        -- /ENTRY:mainCRTStartup is REQUIRED: /SUBSYSTEM:WINDOWS otherwise
        -- defaults to WinMainCRTStartup and this codebase has a plain int main.
        add_ldflags("/SUBSYSTEM:WINDOWS", "/ENTRY:mainCRTStartup", { force = true })
    end

-- The shipping runner target, once the core is wired to STServer.dll.
-- Left commented because it depends on the GUI stack decision recorded in
-- docs/design/SERVER-UI-ARCHITECTURE.md and on the still-unidentified
-- connection-close API noted in INTEGRATION.md section 6.
--
-- target("SkyrimTogetherServerUi")
--     set_kind("binary")
--     set_group("Server")
--     set_basename("SkyrimTogetherServer")
--     add_ldflags("/SUBSYSTEM:WINDOWS", "/ENTRY:mainCRTStartup", { force = true })
--     add_files("src/**.cpp", "gui/**.cpp")
--     add_deps("SkyrimTogetherServer")
--     add_packages("tiltedcore", "spdlog", "libuv", "imgui")
