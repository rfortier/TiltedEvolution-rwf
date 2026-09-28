
#include <windows.h>
#include <string>
#include <OverlayProc.hpp>
#include "ProcessHandler.h"

// CEF spawns this helper as browser_subprocess_path, which lives in the game
// root, so the loader searches that directory first for every DLL the helper and
// libcef.dll resolve by name. The game root is where graphics frameworks keep
// their proxy DLLs, and libcef.dll delay-loads dxgi.dll, d3d11.dll, d3d12.dll and
// dcomp.dll - the names ENB, ReShade, SpecialK and Community Shaders use as their
// entry point. Without the calls below the helper adopts the game's proxy and
// initialises a second copy of that framework inside a process that has no game in
// it.
//
// The screenshot that prompted this names the helper outright. KiLoader - the
// plugin loader KreatE and AELAS plug into - builds its log path from the running
// executable, so the copy inside this helper reports:
//
//   IOException(Couldn't open logging file
//       C:\Users\<user>\AppData\Local\KiLoaderTPProcess\Logs\KiLoader.log)
//   IOException(Couldn't open logging file
//       D:\GAMES\<pack>\STOCK GAME\Data\KiLoader\KiLoader.log)
//       std::system_error: The process cannot access the file because it is
//       being used by another process
//
// "KiLoaderTPProcess" is this executable's own name, and the second file is the
// log the game process already holds for the whole session. Two copies of one
// framework cannot share it, so the second dies during startup and the player is
// told that Skyrim Together is incompatible with KiLoader or KreatE. It is not:
// the helper simply loaded a DLL that was never meant for it, and KiLoader
// reported the collision it was handed.
//
// The chain, for the record: this process starts, libcef.dll delay-loads d3d11.dll
// by name, the game root's ENB proxy wins over System32, ENB loads the satellites
// in enbseries\, and KiLoaderSatelliteENB brings KiLoader in with it.
namespace
{
// Loads one System32 DLL by full path. The loader matches an already-loaded
// module by base name before it searches any directory, so a later bare-name load
// of the same file - which is what a delay-load import does - returns this module
// instead of one found on the search path. Measured: with a stand-in proxy in the
// application directory, the bare-name load fails with ERROR_BAD_EXE_FORMAT (193)
// on its own, and resolves to System32 once the real module is pinned first.
void PreloadSystemDll(const wchar_t* acpName)
{
    wchar_t directory[MAX_PATH];
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    std::wstring path(directory);
    path += L'\\';
    path += acpName;

    LoadLibraryW(path.c_str());
}

// Every name in this list is both delay-loaded by libcef.dll (read from its
// delay-import table) and a name a graphics framework actually ships as its
// game-root entry point: dxgi for ENB and ReShade, d3d11 for SpecialK and
// Community Shaders, d3d12 and dcomp for the newer D3D12 hooks. libcef's other
// delay-loads (USER32, SHELL32, MFPlat and the rest) are system-only names that
// nothing mounts in a game root, so they are deliberately left alone.
//
// The search path itself stays as it is, and that is the point: the overlay's own
// runtime (libEGL.dll, libGLESv2.dll, vk_swiftshader.dll, d3dcompiler_47.dll) is
// deployed into the game root too and is found by name, so
// SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32) would trade one broken
// overlay for another. Pinning these four names is the narrow fix.
//
// The order is load-bearing, not cosmetic. System32's d3d11.dll statically imports
// dxgi.dll, so loading d3d11 first makes it resolve *that* import against the game
// root's proxy and fail with ERROR_BAD_EXE_FORMAT (193) - measured, with stand-in
// proxies in the application directory. dxgi therefore goes first. d3d12.dll and
// dcomp.dll do not import dxgi (also measured), so only the relative order of the
// first two is required; the rest keep the same lowest-level-first order so the
// list stays readable.
void KeepGameRootProxiesOutOfThisProcess()
{
    PreloadSystemDll(L"dxgi.dll");
    PreloadSystemDll(L"d3d11.dll");
    PreloadSystemDll(L"d3d12.dll");
    PreloadSystemDll(L"dcomp.dll");
}
} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Before UIMain, because CefExecuteProcess is where the delay-loads resolve.
    KeepGameRootProxiesOutOfThisProcess();

    return TiltedPhoques::UIMain(lpCmdLine, hInstance, []() { return new ProcessHandler; });
}
