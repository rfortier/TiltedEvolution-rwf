
#include <ScriptExtender.h>
#include <TiltedOnlinePCH.h>
#include <VersionDb.h>

#include <tlhelp32.h>

namespace
{
constexpr wchar_t kScriptExtenderName[] = L"skse64";

constexpr char kScriptExtenderEntrypoint[] = "StartSKSE";

constexpr size_t kScriptExtenderNameLength = sizeof(kScriptExtenderName) / sizeof(wchar_t) - 1;

// AE+ only
// Use this to raise the SKSE baseline
constexpr int kSKSEMinBuild = 20100;

// Pre-AE game versions (1.5.x) run SKSE 2.0.x (file version 0.2.0.x)
constexpr int kSKSEMinBuildPreAE = 20000;

static int GetRequiredSKSEBuild()
{
    int major = 0, minor = 0, revision = 0, build = 0;
    VersionDb::Get().GetLoadedVersion(major, minor, revision, build);

    return (major == 1 && minor < 6) ? kSKSEMinBuildPreAE : kSKSEMinBuild;
}

HMODULE g_SKSEModuleHandle{nullptr};

struct FileVersion
{
    static constexpr uint8_t scVersionSize = 4;
    DWORD versions[scVersionSize];
};

int GetFileVersion(const std::filesystem::path& acFilePath, FileVersion& aVersion)
{
    const auto filename = acFilePath.c_str();

    DWORD dwHandle = 0, sz = GetFileVersionInfoSizeW(filename, &dwHandle);
    if (0 == sz)
    {
        return 1;
    }
    std::string buf(sz, '\0');
    if (!GetFileVersionInfoW(filename, dwHandle, sz, &buf[0]))
    {
        return 2;
    }
    VS_FIXEDFILEINFO* pvi;
    sz = sizeof(VS_FIXEDFILEINFO);
    if (!VerQueryValueA(&buf[0], "\\", reinterpret_cast<LPVOID*>(&pvi), reinterpret_cast<unsigned int*>(&sz)))
    {
        return 3;
    }

    aVersion.versions[0] = pvi->dwProductVersionMS >> 16;
    aVersion.versions[1] = pvi->dwFileVersionMS & 0xFFFF;
    aVersion.versions[2] = pvi->dwFileVersionLS >> 16;
    aVersion.versions[3] = pvi->dwFileVersionLS & 0xFFFF;

    return 0;
}

std::string GetSKSEStyleExeVersion()
{
    // make sure newer than anniversary!
    auto exeBuild = VersionDb::Get().GetLoadedVersionString();
    std::replace(exeBuild.begin(), exeBuild.end(), '.', '_');

    // chop off empty patch numbers for instance "1.6.323.0 becomes "1_6_323"
    auto patchPos = exeBuild.find_last_of("_0");
    if (patchPos != std::string::npos)
    {
        exeBuild.erase(exeBuild.begin() + (patchPos - 1), exeBuild.end());
    }

    return exeBuild;
}
} // namespace

bool IsScriptExtenderLoaded()
{
    return g_SKSEModuleHandle;
}

void LoadScriptExender()
{
    // When the game was launched through the SKSE loader (e.g. via Mod
    // Organizer 2, or our own SKSE plugin), SKSE is already initialized
    // and must not be bootstrapped a second time; just record it so
    // IsScriptExtenderLoaded() reflects reality.
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snap != INVALID_HANDLE_VALUE)
        {
            bool found = false;
            MODULEENTRY32W me{};
            me.dwSize = sizeof(me);
            if (Module32FirstW(snap, &me))
            {
                do
                {
                    if (_wcsnicmp(me.szModule, L"skse64", 6) == 0)
                    {
                        found = true;
                        g_SKSEModuleHandle = me.hModule;
                        break;
                    }
                } while (Module32NextW(snap, &me));
            }
            CloseHandle(snap);

            if (found)
            {
                spdlog::info("Game was launched through the SKSE loader, skipping script extender bootstrap");
                return;
            }
        }
    }

    const auto exeVerson{GetSKSEStyleExeVersion()};

    // Get the path of the game, where the Script Extender dll resides
    const auto gameDir = std::filesystem::current_path();

    std::list<std::filesystem::path> dllMatches;
    for (const auto& dirEntry : std::filesystem::directory_iterator(gameDir))
    {
        const auto& path = dirEntry.path();
        if (path.extension() != L".dll")
            continue;

        auto fileName = path.filename().wstring();
        if (fileName.length() < kScriptExtenderNameLength)
            continue;

        if (fileName.substr(0, kScriptExtenderNameLength) == kScriptExtenderName)
        {
            dllMatches.push_back(path);
        }
    }

    // and before you ask, no, they dont expose it via file version info
    std::filesystem::path* needle = nullptr;
    for (auto& match : dllMatches)
    {
        auto fname = match.filename().string();
        auto ptr = &fname[kScriptExtenderNameLength + 1];
        // make extra sure!
        if (std::strncmp(ptr, exeVerson.c_str(), exeVerson.length()) == 0)
        {
            needle = &match;
            break;
        }
    }

    if (!needle)
        return;

    FileVersion fileVersion;
    if (GetFileVersion(*needle, fileVersion) != 0)
    {
        spdlog::error("Unable to verify Script Extender version");
        return;
    }

    auto skseVersion = fmt::format("v{}.{}.{}.{}", fileVersion.versions[0], fileVersion.versions[1], fileVersion.versions[2], fileVersion.versions[3]);

    // nice try.
    int SkseVCum = fileVersion.versions[0] * 1000000 + fileVersion.versions[1] * 10000 + fileVersion.versions[2] * 100 + fileVersion.versions[3];
    if (SkseVCum < GetRequiredSKSEBuild())
    {
        spdlog::error("Script Extender version is too old for this game version");
        return;
    }

    if (g_SKSEModuleHandle = LoadLibraryW(needle->c_str()))
    {
        if (auto* pStartSKSE = reinterpret_cast<void (*)()>(GetProcAddress(g_SKSEModuleHandle, kScriptExtenderEntrypoint)))
        {
            spdlog::info(
                "Starting SKSE {}... be aware that messages that start without a colored [timestamp] prefix are "
                "logs from the "
                "Script Extender and its loaded mods.",
                skseVersion);
            pStartSKSE();
            spdlog::info("SKSE is active");
        }
        else
            spdlog::warn("SKSE dll doesn't expose StartSKSE(), it may be outdated.");
    }
    else
    {
        spdlog::error("Failed to load {}! Check your privileges or re-download the Script Extender files.", needle->string());
    }
}
