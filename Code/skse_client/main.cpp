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
//
// The prefix alone does not identify the runtime. skse64_steam_loader.dll is
// mapped into the game process as well - it is what patches the game's IAT and
// hooks its WinMain before the runtime is loaded - and it carries the same
// skse64_ prefix while naming no version. So the scan has to keep looking when a
// candidate does not parse instead of stopping at the first name it recognises:
// a scan that broke there returned no version at all whenever the loader came
// first, and the enumeration order of the module list is not ours to choose.
// That silently demoted the authoritative signal to the two fallbacks this
// function exists to replace, on exactly the repacks and downgrades whose exe
// version resource is missing or false.
//
// Code/skse_bootstrap/main.cpp asks the same question of the same list for the
// same reason - it picks which per-version runtime DLL to load - and it already
// continues past a name it cannot parse. The two must not disagree.
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
            break;
        }
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
// is missing or tampered with (common on repacks). The address library a modlist
// carries is the one for the game version it was built against, and this
// repository ships 24 of them, so "the first one found" is not an answer: the
// enumeration order of a directory is not defined, and on the installed game the
// ten pre-AE libraries alone range from 1.5.3 to 1.5.97.
//
// Picking the wrong one is not a near miss. The 1.5.x libraries are not
// interchangeable - comparing 1.5.16 against 1.5.97 over the 777,719 ids they
// share, 767,644 of them (98.7%) resolve to a *different* RVA. That is the same
// fact GamePatch.h records when it refuses to apply a 1.5.97 offset to 1.5.3.
//
// The family is not a guess: this translation unit is compiled twice, once per
// runtime DLL, and the legacy build carries SKYRIM_TARGET_LEGACY. So the file
// this looks at is the one whose name matches the build it is in - every
// `version-*.bin` in the tree is 1.5.x and every `versionlib-*.bin` is
// 1.6.x/1.7.x, and neither build may read the other's.
//
// Within the family, take the highest version present. An address library newer
// than the running game is inert, because the client only resolves the ids it
// knows; an older one resolves them to the wrong addresses. It is still a guess,
// which is why it stays last in the chain and the caller logs what it resolved.
std::string ScanLibraryVersion(const std::filesystem::path& acGameRoot)
{
    const auto plugins = acGameRoot / "Data" / "SKSE" / "Plugins";
    std::error_code ec;
    if (!std::filesystem::exists(plugins, ec))
        return {};

#ifdef SKYRIM_TARGET_LEGACY
    constexpr bool kLegacy = true;
#else
    constexpr bool kLegacy = false;
#endif

    int best[4] = {-1, -1, -1, -1};
    for (auto it = std::filesystem::directory_iterator(plugins, ec);
         it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        const std::string name = it->path().filename().string();
        int a = 0, b = 0, c = 0, d = 0;
        const bool legacyName = sscanf_s(name.c_str(), "version-%d-%d-%d-%d.bin", &a, &b, &c, &d) == 4;
        const bool modernName = sscanf_s(name.c_str(), "versionlib-%d-%d-%d-%d.bin", &a, &b, &c, &d) == 4;
        if (!legacyName && !modernName)
            continue;
        if (legacyName != kLegacy)
            continue;

        const int candidate[4] = {a, b, c, d};
        for (int i = 0; i < 4; i++)
        {
            if (candidate[i] != best[i])
            {
                if (candidate[i] > best[i])
                {
                    best[0] = a;
                    best[1] = b;
                    best[2] = c;
                    best[3] = d;
                }
                break;
            }
        }
    }

    if (best[0] < 0)
        return {};

    char buf[64];
    sprintf_s(buf, "%d.%d.%d.%d", best[0], best[1], best[2], best[3]);
    return buf;
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
