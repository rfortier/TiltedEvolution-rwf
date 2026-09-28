// The client runtime DLL loaded by the SKSE bootstrap plugin
// (SkyrimTogetherSKSE.dll) after it deployed this file and the rest of the
// runtime payload into the game root. STClient_Bootstrap performs the same
// boot the SkyrimTogether.exe launcher does: address library load, engine
// hooks, app start.

#include <Windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

// TiltedPhoques::String is declared in TiltedCore's Stl.hpp
#include <TiltedCore/Stl.hpp>

// defined in the statically linked SkyrimTogetherClient library (Code/client/main.cpp)
extern void RunTiltedInit(const std::filesystem::path& acGamePath, const TiltedPhoques::String& aExeVersion);
extern void RunTiltedApp();

// client -> launcher externals that the SKSE launch flow must provide.
// these live in the global namespace with external linkage: the client
// library references them by mangled name.
HICON g_SharedWindowIcon = nullptr;

// the launcher served jit stub allocations from a buffer adjacent to the
// manually mapped exe; here the game is loaded normally, so scan for free
// executable pages within +-1GB of the game module (x64 rip-relative range)
void* RipAllocateN(size_t blockLength)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t step = 0x10000;
    const uintptr_t minAddr = base > 0x40000000ull ? base - 0x40000000ull : step;
    const uintptr_t maxAddr = base + 0x40000000ull;

    for (uintptr_t addr = maxAddr; addr >= minAddr; addr -= step)
    {
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(addr), blockLength, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
            return p;
    }

    // last resort: any executable page
    return VirtualAlloc(nullptr, blockLength, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}

namespace
{
std::string QueryGameVersion()
{
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
        return {};

    if (DWORD infoSize = GetFileVersionInfoSizeW(exePath, nullptr))
    {
        auto buffer = std::make_unique<uint8_t[]>(infoSize);
        if (GetFileVersionInfoW(exePath, 0, infoSize, buffer.get()))
        {
            char* version = nullptr;
            DWORD len = 0;
            if (VerQueryValueA(buffer.get(), "\\StringFileInfo\\040904B0\\ProductVersion",
                               reinterpret_cast<void**>(&version), reinterpret_cast<PUINT>(&len)) &&
                version && *version)
            {
                return version;
            }

            if (VerQueryValueA(buffer.get(), "\\StringFileInfo\\040904B0\\FileVersion",
                               reinterpret_cast<void**>(&version), reinterpret_cast<PUINT>(&len)) &&
                version && *version)
            {
                return version;
            }
        }
    }

    return {};
}

// The loaded SKSE runtime is built for exactly one game version and its
// module name embeds it (skse64_1_5_97.dll -> "1.5.97.0"). Downgrader mods
// overwrite the exe and can strip its version resource, so this is the most
// reliable signal; the exe strings and the address library scan are
// fallbacks.
std::string GetSKSEModuleVersion()
{
    std::string result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return result;
    MODULEENTRY32W me{ sizeof(me) };
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
    {
        const std::wstring name = me.szModule;
        if (name.rfind(L"skse64_", 0) != 0)
            continue;
        int a = 0, b = 0, c = 0;
        if (swscanf_s(name.c_str() + 7, L"%d_%d_%d", &a, &b, &c) >= 3)
        {
            char buf[64];
            sprintf_s(buf, "%d.%d.%d.0", a, b, c);
            result = buf;
        }
        break;
    }
    CloseHandle(snap);
    return result;
}

// The spdlog logger is only up once TiltedOnlineApp is constructed, so boot
// failures before that point would otherwise be completely silent (observed
// as "F2 does nothing, no error"). Append step markers to st_boot.log.
void AppendBootLog(const std::filesystem::path& acGameRoot, const std::string& acLine)
{
    const auto log = acGameRoot / "st_boot.log";
    FILE* f = nullptr;
    if (fopen_s(&f, log.string().c_str(), "a") == 0 && f)
    {
        fprintf(f, "%s\n", acLine.c_str());
        fclose(f);
    }
}

// Fall back to the address library file names when the exe version resource
// is missing or tampered with (common on repacks): Data/SKSE/Plugins ships
// exactly one set of bins for the installed game version.
std::string ScanLibraryVersion(const std::filesystem::path& acGameRoot)
{
    const auto plugins = acGameRoot / "Data" / "SKSE" / "Plugins";
    std::error_code ec;
    if (!std::filesystem::exists(plugins, ec))
        return {};
    for (auto it = std::filesystem::directory_iterator(plugins, ec);
         it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        const std::string name = it->path().filename().string();
        int a = 0, b = 0, c = 0, d = 0;
        // versionlib-1-6-1170-0.bin  or  version-1-5-97-0.bin
        if (sscanf_s(name.c_str(), "versionlib-%d-%d-%d-%d.bin", &a, &b, &c, &d) == 4 ||
            sscanf_s(name.c_str(), "version-%d-%d-%d-%d.bin", &a, &b, &c, &d) == 4)
        {
            char buf[64];
            sprintf_s(buf, "%d.%d.%d.%d", a, b, c, d);
            return buf;
        }
    }
    return {};
}
} // namespace

extern "C" __declspec(dllexport) bool STClient_Bootstrap(const wchar_t* acpGameRoot)
{
    if (!acpGameRoot)
        return false;

    // the launcher raises the stdio handle limit before booting the client
    // (cef and the game open a lot of handles); keep parity here
    _setmaxstdio(8192);

    const std::filesystem::path gamePath(acpGameRoot);

    const auto exeVersion = QueryGameVersion();
    // SKSE is version-locked to the game, so prefer its module name when a
    // downgrader/other mod replaced the exe; exe strings and finally the
    // address library file names are the fallbacks
    auto version = GetSKSEModuleVersion();
    if (version.empty())
        version = exeVersion;
    if (version.empty())
        version = ScanLibraryVersion(gamePath);

    AppendBootLog(gamePath, "boot: entered, exe version='" + exeVersion +
                                "', resolved='" + version + "'");
    if (version.empty())
    {
        AppendBootLog(gamePath, "boot: no usable game version, aborting");
        return false;
    }

    AppendBootLog(gamePath, "boot: RunTiltedInit");
    RunTiltedInit(gamePath, version.c_str());
    AppendBootLog(gamePath, "boot: RunTiltedApp");
    RunTiltedApp();

    return true;
}
