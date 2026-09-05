
#include <TiltedOnlineApp.h>
#include <TiltedOnlinePCH.h>

#include <Commctrl.h>
#include <Windows.h>

#include <base/dialogues/win/TaskDialog.h>

#include <GameRoot.h>

std::unique_ptr<TiltedOnlineApp> g_appInstance{nullptr};

extern HICON g_SharedWindowIcon;

namespace
{
std::filesystem::path s_gameRoot;
} // namespace

const std::filesystem::path& GetGameRoot()
{
    return s_gameRoot;
}

static void ShowAddressLibraryError(const wchar_t* apGamePath, const String& acExeVersion)
{
    const bool isLegacyGame = strncmp(acExeVersion.c_str(), "1.5.", 4) == 0;

    auto errorDetail = fmt::format(L"Looking for it here: {}\\Data\\SKSE\\Plugins", apGamePath);

    const wchar_t* mainText = L"Make sure to use \"All in one (1.6.X)\"";
    if (isLegacyGame)
        mainText = L"Game version 1.5.x requires version-<version>.bin (from \"All in one (Special Edition)\") "
                   L"and the AE-to-SE id mapping file versionlib-ae-to-se-<version>.map";

    Base::TaskDialog dia(g_SharedWindowIcon, L"Error", L"Failed to load Skyrim Address Library", mainText, errorDetail.c_str());

    dia.AppendButton(0xBEEF, L"Visit Address Library modpage on nexusmods.com");
    const int result = dia.Show();
    if (result == 0xBEEF)
    {
        ShellExecuteW(nullptr, L"open", LR"(https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files)", nullptr, nullptr, SW_SHOWNORMAL);
    }

    exit(4);
}

void RunTiltedInit(const std::filesystem::path& acGamePath, const String& aExeVersion)
{
    s_gameRoot = acGamePath;

    if (!VersionDb::Get().Load(acGamePath, aExeVersion))
    {
        ShowAddressLibraryError(acGamePath.c_str(), aExeVersion);
    }

    // VersionDb::Get().DumpToTextFile(R"(S:\Work\Tilted\fallout\_addresslib.txt)");

    // The TiltedOnlineApp constructor installs the file logger, so anything
    // worth keeping has to be logged after this point.
    g_appInstance = std::make_unique<TiltedOnlineApp>();

    // Which address library got picked decides whether the hooks below can
    // land at all, so record it: a wrong or partial pick then shows up here
    // instead of as a silent "F2 does nothing".
    spdlog::info("address library loaded: game {}, {} ids, {}", VersionDb::Get().GetLoadedVersionString(),
                 VersionDb::Get().GetOffsetMap().size(),
                 VersionDb::Get().IsLegacyFormat() ? "pre-AE library + AE id map" : "AE library");

    if (VersionDb::Get().IsLegacyFormat())
    {
        // Legacy 1.5.x: 99.7% of the id references are mapped. The eight that
        // are not resolve to no-op stubs, RTTI lookups are null-guarded, and a
        // patch site with no 1.5.x offset is skipped rather than aimed at the
        // 1.6.x one (Games/GamePatch.h) - see Tools/missing_1_5_97_ids.txt for
        // what each of them costs. Struct member offsets are compiled for
        // 1.6.x and a few differ on 1.5.x (Actor's two flags fields sit 8
        // bytes earlier), so treat any 1.5.x crash as worth reporting rather
        // than assuming a bug.
        spdlog::warn("legacy game version detected (1.5.x): unmapped ids degrade to stubs, "
                     "1.6.x-only patches are skipped, struct offsets may differ.");
    }

    TiltedOnlineApp::InstallHooks2();
    TP_HOOK_COMMIT;
}

void RunTiltedApp()
{
    g_appInstance->BeginMain();
}
