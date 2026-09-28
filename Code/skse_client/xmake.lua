local function build_runtime_dll(name, basename, client_lib, extra_defines)
target(name)
    set_basename(basename)
    set_kind("shared")
    set_group("Client")
    set_symbols("debug", "hidden")
    add_defines("TARGET_PREFIX=\"st\"")
    if extra_defines then
        for _, d in ipairs(extra_defines) do
            add_defines(d)
        end
    end

    add_includedirs(
        ".",
        "../",
        "../../Libraries/")
    add_headerfiles("**.h")
    add_files("**.cpp")

    -- whole-archive so the client's self-registering systems (hooks,
    -- animation graph descriptors, rtti) are all pulled in
    add_deps(client_lib)
    add_ldflags("/WHOLEARCHIVE:" .. client_lib, { force = true })

    add_deps(
        "TiltedReverse",
        "TiltedHooks",
        "TiltedUi",
        "ImGuiImpl",
        "CommonLib")

    add_syslinks(
        "user32",
        "shell32",
        "comdlg32",
        "bcrypt",
        "ole32",
        "dxgi",
        "d3d11",
        "gdi32",
        "SetupAPI",
        "Powrprof",
        "Cfgmgr32",
        "Propsys",
        "version",
        "delayimp")

    add_packages(
        "tiltedcore",
        "spdlog",
        "minhook",
        "hopscotch-map",
        "cryptopp",
        "glm",
        "cef",
        "mem")

    -- TiltedHooks.lib contains /GL objects, so this link needs /LTCG (a
    -- /FORCE:MULTIPLE restart is not compatible with it); the duplicate
    -- definitions it used to mask were fixed at the source (inline)
    add_ldflags(
        "/LTCG",
        "/IGNORE:4254,4006",
        "/INCREMENTAL:NO", { force = true })
end

-- modern game versions (1.6.x / 1.7.x)
build_runtime_dll("SkyrimTogetherClientDll", "SkyrimTogetherRuntime", "SkyrimTogetherClient", nil)
-- legacy game versions (1.5.x): compiled against the 1.5.x struct layouts.
-- The SKSE bootstrap picks this DLL when the game version is 1.5.x.
build_runtime_dll("SkyrimTogetherClientDllLegacy", "SkyrimTogetherRuntime_1_5", "SkyrimTogetherClientLegacy", {"SKYRIM_TARGET_LEGACY=1"})
