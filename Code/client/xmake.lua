
local function build_client(name, extra_defines)
target(name)
    set_kind("static")
    set_group("Client")
    if extra_defines then
        for _, d in ipairs(extra_defines) do
            add_defines(d)
        end
    end
    add_includedirs(
        ".",
        "../../Libraries/",
        -- the STRPM consumer contract: the client implements the transport that
        -- companion plugins call through, so both compile against one header
        "../plugins/STRPM/include")
    set_pcxxheader("TiltedOnlinePCH.h")

    -- exclude game specifc stuff
    add_headerfiles("**.h|Games/Skyrim/**")
    add_files("**.cpp|Games/Skyrim/**")

    after_install(function(target)
        -- copy dlls
        for _, pkg_with_dlls in ipairs({"cef", "discord"}) do
            local linkdir = target:pkg(pkg_with_dlls):get("linkdirs")
            local bindir = path.join(linkdir, "..", "bin")
            os.cp(bindir, target:installdir())
        end
        -- copy ui
        local uidir = path.join(target:scriptdir(), "..", "skyrim_ui", "src")
        os.cp(path.join(uidir, "assets", "images", "cursor.dds"), path.join(target:installdir(), "bin", "assets", "images", "cursor.dds"))
        os.cp(path.join(uidir, "assets", "images", "cursor.png"), path.join(target:installdir(), "bin", "assets", "images", "cursor.png"))
        os.rm(path.join(target:installdir(), "bin", "**Tests.exe"))
    end)

    add_files("Games/Skyrim/**.cpp")
    add_headerfiles("Games/Skyrim/**.h")
    -- rather hacky:
    add_includedirs("Games/Skyrim")
    add_deps("SkyrimEncoding")
    add_deps(
        "UiProcess",
        "CommonLib",
        "BaseLib",
        "ImGuiImpl",
        "TiltedConnect",
        "TiltedReverse",
        "TiltedHooks",
        "TiltedUi",
        {inherit = true}
    )

    add_packages(
        "tiltedcore",
        "spdlog",
        "hopscotch-map",
        "cryptopp",
        "gamenetworkingsockets",
        "discord",
        "imgui",
        "cef",
        "minhook",
        "entt",
        "glm",
        "mem",
        "xbyak")

    add_syslinks(
        "version",
        "dbghelp",
        -- EnumProcessModules/GetModuleInformation, used by the crash report to
        -- place an address that resolves to no module
        "psapi",
        "kernel32")
end

add_requires("tiltedcore")

build_client("SkyrimTogetherClient")
-- per-version client library for legacy game versions (1.5.x): compiles the
-- engine structs with the 1.5.x layouts (see the SKYRIM_TARGET_LEGACY
-- conditionals in Games/Skyrim). The matching runtime DLL is loaded by the
-- SKSE bootstrap based on the game version.
build_client("SkyrimTogetherClientLegacy", {"SKYRIM_TARGET_LEGACY=1"})
