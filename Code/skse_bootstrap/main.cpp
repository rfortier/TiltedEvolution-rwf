// SKSE bootstrap plugin (Scheme A - self deploying).
//
// This DLL is intentionally dependency free (kernel32 only) so the OS loader
// can always resolve it. On load it:
//   1. derives the game root from the running exe path
//   2. syncs the runtime payload from Data/SkyrimTogetherRuntime/ (virtualized
//      by MO2's usvfs when installed as a mod, a real folder otherwise) into
//      the game root - files that are missing or have an older version get
//      copied, so mod updates propagate automatically
//   3. loads the client DLL (SkyrimTogether.dll) from the game root and calls
//      its exported bootstrap entry, which performs the address library load
//      and engine hooking.
//
// If the payload is unavailable (bare install) or a file cannot be deployed
// (permissions/AV), a task dialog lists exactly what is missing so the user
// can fix it manually instead of failing silently.

#include <Windows.h>
#include <winver.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Mirrors SKSEPluginVersionData from SKSE64's PluginAPI.h. Kept private so this
// dll stays dependency free - it must load before anything has been deployed.
//
// Declared outside the anonymous namespace on purpose: it types an exported
// variable, and a type with internal linkage on an exported symbol is a
// linkage mismatch the standard does not sanction.
//
// SKSE 2.2.x does NOT call SKSEPlugin_Query. PluginManager::ScanPlugins looks up
// this export by name and, when it is absent, logs "no version data" and never
// adds the plugin to the load list - there is no legacy fallback. That is
// exactly what SkyrimTogetherSKSE.dll hit on 1.6.1170: the log line
// "plugin SkyrimTogetherSKSE.dll (00000000  00000000) no version data 0
// (handle 0)" is SKSE saying it skipped the file, so SKSEPlugin_Load never ran,
// the runtime was never deployed, and the F2 menu never appeared while the esp
// files still showed up in the Creations menu.
//
// SKSE 2.0.x (1.5.97) never looks for this export and still uses
// SKSEPlugin_Query, which is why the same dll kept working there.
//
// This dll resolves no engine addresses and touches no game structure - it only
// deploys files and loads the real runtime - so it declares version
// independence rather than a list of runtimes. That is what the companion
// plugins do, and for the same reason: a list would have to be edited and the
// dll rebuilt for every game patch, and it would reject the runtimes this
// package already supports but that nobody remembered to add (1.6.1179 GOG,
// 1.6.659 GOG, 1.6.678 Epic). The version gate that matters is inside
// SkyrimTogetherRuntime.dll, which resolves addresses through the address
// library and refuses a runtime it has no library for.
//
// The flag pair is not interchangeable:
//   kVersionIndependent_Signatures says "no address library dependency", which
//     skips SKSE's CheckAddressLibrary and, on 1.7.99+, the AddressLibraryV5
//     requirement that would otherwise reject this dll with "must be
//     recompiled for new address library".
//   kVersionIndependentEx_NoStructUse is required alongside it, because any
//     version-independent plugin on 1.6.629+ is rejected with "only compatible
//     with versions earlier than 1.6.629" unless it either claims the post-629
//     structure layout or declares that it uses no structures at all.
struct SksePluginVersionData
{
    enum
    {
        kVersion = 1,

        kVersionIndependent_Signatures = 1 << 1,
        kVersionIndependentEx_NoStructUse = 1 << 0,
    };

    uint32_t dataVersion;
    uint32_t pluginVersion;
    char name[256];
    char author[256];
    char supportEmail[252];
    uint32_t versionIndependenceEx;
    uint32_t versionIndependence;
    uint32_t compatibleVersions[16];
    uint32_t seVersionRequired;
};

// This struct is a hand-written mirror of SKSE64's PluginVersionData
// (skse64/PluginAPI.h), and SKSE reads it straight out of this dll's exports by
// offset - it never sees these names. Every other mirrored ABI in this tree pins
// its layout (the ExtraDataList and GameVM branches assert their offsets, the
// patch sites carry the build they were measured on), and this one only asserted
// its flag values. A field added here, or an array resized, would move every
// field after it and SKSE would read the name as the author and the flags out of
// the version list - silently, because the export is still present and still
// called SKSEPlugin_Version. sizeof is the whole check: it is 848 for the layout
// above and any edit that keeps it is an edit SKSE cannot see.
static_assert(sizeof(SksePluginVersionData) == 848,
              "the layout must stay identical to SKSE64's PluginVersionData; a changed size means "
              "SKSE reads every field after the change from the wrong offset");
static_assert(offsetof(SksePluginVersionData, name) == 8, "name follows the two version words");
static_assert(offsetof(SksePluginVersionData, versionIndependenceEx) == 772,
              "the flags sit after the three strings; moving them changes what SKSE reads");
static_assert(offsetof(SksePluginVersionData, seVersionRequired) == 844,
              "the last word is the required game version");

namespace
{
// skse64 plugin api (stable ABI since 2.0)
struct SksePluginInfo
{
    uint32_t infoVersion;
    const char* name;
    uint32_t version;
};

constexpr uint32_t kSksePluginInfoVersion = 1;

constexpr wchar_t kRuntimeDirName[] = L"SkyrimTogetherRuntime";
constexpr wchar_t kClientDllName[] = L"SkyrimTogetherRuntime.dll";
constexpr wchar_t kClientDllLegacyName[] = L"SkyrimTogetherRuntime_1_5.dll";
constexpr char kClientEntry[] = "STClient_Bootstrap";
constexpr wchar_t kVersionMarker[] = L".str_version";

std::filesystem::path GetGameRoot()
{
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
        return {};
    std::filesystem::path p(exePath);
    return p.parent_path();
}

// The 1.5.x engine ships a different struct layout (ExtraDataList has no
// vtable, several engine structs sit 8 bytes earlier), so the runtime is
// built twice: SkyrimTogetherRuntime.dll (1.6.x/1.7.x) and
// SkyrimTogetherRuntime_1_5.dll (1.5.x).
//
// The loaded SKSE runtime is built for exactly one game version and its
// module name embeds it (skse64_1_5_97.dll, skse64_1_6_1170.dll ...).
// Downgrader mods overwrite the exe and can even strip its version
// resource, so the SKSE module is the authoritative signal; the exe version
// is only a fallback.
bool IsLegacyGame()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snap != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W me{ sizeof(me) };
        for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
        {
            const std::wstring name = me.szModule; // e.g. "skse64_1_5_97.dll"
            if (name.rfind(L"skse64_", 0) != 0)
                continue;
            int major = 0, minor = 0;
            if (swscanf_s(name.c_str() + 7, L"%d_%d", &major, &minor) >= 2)
            {
                CloseHandle(snap);
                return major == 1 && minor == 5;
            }
        }
        CloseHandle(snap);
    }

    // Fallback: the exe version. The *structured* VS_FIXEDFILEINFO is
    // unreliable on cracked builds (CODEX stamps it 1.0.0.0 while the
    // StringFileInfo entries still read 1.5.97.0), so read the strings first.
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
        return false;

    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(exePath, &handle);
    if (size == 0)
        return false;

    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(exePath, 0, size, data.data()))
        return false;

    // ProductVersion first, FileVersion as fallback (same order as the
    // client's QueryGameVersion)
    const wchar_t* kVersionKeys[] = {
        L"\\StringFileInfo\\040904B0\\ProductVersion",
        L"\\StringFileInfo\\040904B0\\FileVersion",
    };
    for (const wchar_t* key : kVersionKeys)
    {
        wchar_t* value = nullptr;
        UINT len = 0;
        if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &len) && value && *value)
        {
            int major = 0, minor = 0;
            if (swscanf_s(value, L"%d.%d", &major, &minor) >= 2)
                return major == 1 && minor == 5;
        }
    }

    // last resort: the structured version (fine on official builds)
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&ffi), &len) || !ffi)
        return false;

    return HIWORD(ffi->dwFileVersionMS) == 1 && LOWORD(ffi->dwFileVersionMS) == 5;
}

// Compares two files by content, in chunks, so a multi-hundred-megabyte dll is
// never held in memory at once. Only called when the sizes already match, so a
// size mismatch is handled by the caller and never reaches here.
bool SameContents(const std::filesystem::path& acLeft, const std::filesystem::path& acRight)
{
    std::ifstream left(acLeft, std::ios::binary);
    std::ifstream right(acRight, std::ios::binary);
    if (!left.is_open() || !right.is_open())
        return false;

    constexpr std::streamsize kChunk = 64 * 1024;
    std::vector<char> a(static_cast<size_t>(kChunk));
    std::vector<char> b(static_cast<size_t>(kChunk));

    for (;;)
    {
        left.read(a.data(), kChunk);
        right.read(b.data(), kChunk);

        const auto leftRead = left.gcount();
        const auto rightRead = right.gcount();

        if (leftRead != rightRead)
            return false;
        if (leftRead == 0)
            return true;
        if (std::memcmp(a.data(), b.data(), static_cast<size_t>(leftRead)) != 0)
            return false;
    }
}

// Reads a deploy marker into a build stamp.
//
// The format is a single line: "<commit> <branch>", where commit is the short git
// sha the payload was built from (BUILD_COMMIT in BuildInfo.h) and branch is the git
// branch name. Both are written by the packaging scripts, which used to create this
// file empty - see the comment at the marker write site for why the empty case is
// still accepted.
//
// Returns false when the file cannot be read. An empty or unparsable file returns
// true with an empty commit, which callers treat as "no stamp available" rather than
// as an error.
bool ReadBuildStamp(const std::filesystem::path& acMarker, std::string& aCommit, std::string& aBranch)
{
    aCommit.clear();
    aBranch.clear();

    std::ifstream file(acMarker, std::ios::binary);
    if (!file.is_open())
        return false;

    // Bounded: a marker is one short line, and this runs before the game starts. A
    // file larger than this is not a stamp; it is read as empty rather than trusted.
    constexpr std::streamsize kMaxMarkerBytes = 256;

    std::string line;
    line.resize(static_cast<size_t>(kMaxMarkerBytes));

    file.read(line.data(), kMaxMarkerBytes);
    line.resize(static_cast<size_t>(file.gcount()));

    // Strip a trailing newline and any CR so a file written with CRLF parses the same
    // way as one written with LF.
    while (!line.empty() && (line.back() == static_cast<char>(10) || line.back() == static_cast<char>(13)))
        line.pop_back();

    const auto separator = line.find(static_cast<char>(32));
    if (separator == std::string::npos)
    {
        aCommit = line;
        return true;
    }

    aCommit = line.substr(0, separator);
    aBranch = line.substr(separator + 1);
    return true;
}

// True when the payload in the game root is the one this launch would deploy.
//
// Two payloads match when their build stamps name the same commit. A stamp that is
// empty on either side - the shape every package shipped so far has - falls back to
// comparing the bytes, which is what this check did before the stamp carried
// anything.
//
// The stamp is what makes this a real version check: comparing marker bytes meant two
// different builds that happened to write the same marker compared equal, and the
// payload in the game root was then never refreshed.
bool SameBuild(const std::filesystem::path& acPayloadMarker, const std::filesystem::path& acDeployedMarker)
{
    std::string payloadCommit, payloadBranch;
    std::string deployedCommit, deployedBranch;

    const bool payloadRead = ReadBuildStamp(acPayloadMarker, payloadCommit, payloadBranch);
    const bool deployedRead = ReadBuildStamp(acDeployedMarker, deployedCommit, deployedBranch);

    if (!payloadRead || !deployedRead)
        return false;

    // Both sides carry a stamp: the commits are the whole answer. The branch is not
    // compared - a hotfix built from a release branch is still the same code.
    if (!payloadCommit.empty() && !deployedCommit.empty())
        return payloadCommit == deployedCommit;

    // One side is unstamped (a package from before the stamp existed, or a marker
    // that only ever recorded its own presence). Compare the bytes so those packages
    // keep deploying exactly as they did.
    return SameContents(acPayloadMarker, acDeployedMarker);
}

// Deploys one payload file, reporting the error of the step that actually
// failed through aError.
//
// Why the error is captured here instead of by the caller: this function's
// failure path used to end with DeleteFileW(<target>.str_old), and nothing in
// the code base ever creates a .str_old file - so that call always failed and
// left ERROR_FILE_NOT_FOUND (2) as the thread's last error. The caller then
// reported that, which is why every real failure - a locked target, a denied
// write, a full disk - showed up in st_deploy_error.log as "error 2". That
// sends everyone hunting for a file that is present and merely in use.
// Defined below, next to the rest of the boot logging. Declared here because
// DeployFile reports a failure mode that is otherwise invisible.
void AppendLog(const std::filesystem::path& acFile, const std::wstring& acLine);

bool DeployFile(const std::filesystem::path& acSource, const std::filesystem::path& acTarget, DWORD& aError)
{
    aError = ERROR_SUCCESS;

    std::error_code ec;
    if (!std::filesystem::exists(acTarget, ec))
    {
        if (std::filesystem::copy_file(acSource, acTarget, std::filesystem::copy_options::overwrite_existing, ec))
            return true;
        aError = ec.value() != 0 ? static_cast<DWORD>(ec.value()) : GetLastError();
        return false;
    }

    // Decide whether the target already *is* the payload.
    //
    // The test is content first, timestamps only as a cheap pre-filter. It used
    // to be the other way round - "skip when the target is not older than the
    // source" - and that silently redeployed files whose content never changed.
    // A mod package extracted by Mod Organizer, or copied by an archive tool
    // that restores stored timestamps, gives the payload a *newer* mtime than
    // the file already in the game root even when the bytes are identical, so
    // every launch copied that file again and then failed to swap it, because
    // the game had it loaded. d3dcompiler_47.dll is exactly this case: it is
    // what produced the permanent "failed: d3dcompiler_47.dll" line and the
    // .str_new file that sat in the game root for days.
    //
    // A mismatching SIZE means an earlier copy was interrupted (permissions,
    // crash, AV) and left a truncated file behind - a truncated dll makes
    // LoadLibrary fail with ERROR_INVALID_DATATYPE (182), so always re-copy.
    std::error_code srcSizeEc, dstSizeEc;
    const auto srcSize = std::filesystem::file_size(acSource, srcSizeEc);
    const auto dstSize = std::filesystem::file_size(acTarget, dstSizeEc);
    if (!srcSizeEc && !dstSizeEc && srcSize != 0 && srcSize == dstSize)
    {
        // Separate error codes again, and this one matters: a failed call
        // returns a default-constructed timestamp, so sharing a single code
        // would let a successful read of the *target* clear an error from the
        // read of the *source*, and the default-constructed source time would
        // then compare as "not newer" - reporting a file as up to date on the
        // strength of a timestamp that was never read.
        std::error_code srcTimeEc, dstTimeEc;
        const auto srcTime = std::filesystem::last_write_time(acSource, srcTimeEc);
        const auto dstTime = std::filesystem::last_write_time(acTarget, dstTimeEc);

        // Same timestamp: the target was deployed from this very payload, so the
        // content cannot differ and reading a 250MB dll on every launch is not
        // worth it. Only when the payload looks newer is the content compared.
        if (!srcTimeEc && !dstTimeEc && srcTime <= dstTime)
            return true;

        if (SameContents(acSource, acTarget))
        {
            // Identical bytes, so the target is already correct. Touch its timestamp
            // forward to the payload's so later launches take the cheap path above
            // instead of hashing the file again.
            //
            // When that touch fails - a Mod Organizer deployment that serves the file
            // read-only is the usual reason - this branch is taken again on every
            // launch and the file is compared byte for byte each time. That is where
            // the multi-hundred-megabyte read comes from, and it used to leave no
            // trace at all: DeployFile still returns true, so the caller has no way
            // to tell this apart from a healthy deployment.
            //
            // Reported once per boot rather than once per file; a modlist that trips
            // this trips it for dozens of files at once, and a line each would bury
            // the rest of the log.
            std::error_code touchEc;
            std::filesystem::last_write_time(acTarget, srcTime, touchEc);
            if (touchEc)
            {
                static bool s_reportedTouchFailure = false;
                if (!s_reportedTouchFailure)
                {
                    s_reportedTouchFailure = true;
                    AppendLog(acTarget.parent_path() / L"st_deploy_error.log",
                              L"[deploy] could not advance the timestamp on " + acTarget.wstring() +
                                  L" (error " + std::to_wstring(touchEc.value()) +
                                  L"); its contents will be compared again on every launch");
                }
            }

            // Any staging file next to it is a copy of a *previous* payload that
            // this one supersedes, and leaving it behind is how the game root
            // accumulated a .str_new that no launch would ever consume. It is
            // only removed now that the target is known to match the payload.
            std::filesystem::path stale = acTarget;
            stale += L".str_new";
            std::error_code removeEc;
            std::filesystem::remove(stale, removeEc);

            return true;
        }
    }

    // Stage the payload next to the target and swap it in.
    //
    // MOVEFILE_REPLACE_EXISTING, not delete-then-move: when the target is
    // locked - it is a loaded dll, or the game is still shutting down - the move
    // fails with ERROR_ACCESS_DENIED and leaves the target untouched, so a
    // failed swap can never leave the game root without a dll. A delete would
    // succeed and then the move would fail, which is exactly how a game folder
    // ends up missing libcef.dll.
    std::filesystem::path tmp = acTarget;
    tmp += L".str_new";

    // A .str_new left behind by an earlier launch is the *staged payload*, so
    // reuse it when it is already current instead of copying the same multi-
    // hundred-megabyte file again on every single launch while the target stays
    // locked. The staged copy is only trusted when it is at least as new as the
    // payload AND the same size - a stale leftover can then never downgrade the
    // target, and a truncated leftover (the failure mode the size check above
    // exists for) is never promoted over a good payload.
    bool staged = false;
    {
        // One error_code per call: these calls clear the code they are handed on
        // success, so sharing a single one would let a later success mask an
        // earlier failure and compare against a default-constructed timestamp.
        std::error_code timeEc, sizeEc, srcEc;
        const auto tmpTime = std::filesystem::last_write_time(tmp, timeEc);
        const auto tmpSize = std::filesystem::file_size(tmp, sizeEc);
        const auto srcTime = std::filesystem::last_write_time(acSource, srcEc);
        staged = !timeEc && !sizeEc && !srcEc && tmpTime >= srcTime && tmpSize == srcSize;
    }

    if (staged || std::filesystem::copy_file(acSource, tmp, std::filesystem::copy_options::overwrite_existing, ec))
    {
        if (MoveFileExW(tmp.c_str(), acTarget.c_str(), MOVEFILE_REPLACE_EXISTING))
            return true;

        aError = GetLastError();
    }
    else
    {
        aError = ec.value() != 0 ? static_cast<DWORD>(ec.value()) : GetLastError();
    }

    // The swap was deferred: the .str_new file stays in place and the next
    // launch promotes it (see the note at the top of this function for why the
    // real error, and not a fabricated ERROR_FILE_NOT_FOUND, is reported).
    return false;
}

// Appends a line to a log file in the game root, used for deploy and
// client-loader diagnostics so a failure is never silent. UTF-8, because the
// client appends to st_boot.log too and writes narrow text there - two
// encodings in one file made it unreadable in any editor.
void AppendLog(const std::filesystem::path& acFile, const std::wstring& acLine)
{
    const std::wstring line = acLine + L"\r\n";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return;

    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                        utf8.data(), bytes, nullptr, nullptr);

    const HANDLE f = CreateFileW(acFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                                 nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(f);
}

// Where this dll was loaded from. Under Mod Organizer the SKSE plugin comes
// out of the *virtual* Data\SKSE\Plugins; a real path means someone copied it
// into the game folder by hand, in which case the rest of the mod may not be
// installed in MO2 at all - which looks exactly like a missing payload.
std::wstring SelfPath()
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&SelfPath), &self))
        return L"<unknown>";
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self, path, MAX_PATH);
    return path;
}

// Mod Organizer runs the game inside its virtual file system; without it the
// mod's files are simply not there, no matter how the mod list looks.
bool IsVirtualFileSystemActive()
{
    return GetModuleHandleW(L"usvfs_x64.dll") || GetModuleHandleW(L"usvfs.dll");
}

// Immediate children of the Data folder, as the game process sees them.
std::wstring DescribeDataFolder(const std::filesystem::path& acGameRoot)
{
    const auto data = acGameRoot / L"Data";
    std::error_code ec;
    if (!std::filesystem::exists(data, ec))
        return L"Data does not exist";

    std::wstring names;
    size_t shown = 0;
    for (auto it = std::filesystem::directory_iterator(data, ec);
         it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        if (!it->is_directory(ec))
            continue;
        if (shown++ >= 24)
        {
            names += L", ...";
            break;
        }
        if (!names.empty())
            names += L", ";
        names += it->path().filename().wstring();
    }
    return names.empty() ? L"Data has no subfolders" : names;
}

bool DeployRuntime(const std::filesystem::path& acGameRoot, const std::filesystem::path& acPayload,
                   std::vector<std::wstring>& aFailures)
{
    // The marker travels with the payload. Its contents are never parsed - it is a
    // build stamp compared byte for byte against the copy left in the game root after
    // a successful deploy, which is what makes the short circuit below a version
    // check rather than a guess. A marker that is missing means the payload folder is
    // not installed at all.
    std::filesystem::path marker = acPayload / kVersionMarker;
    if (!std::filesystem::exists(marker))
    {
        aFailures.push_back(L"<payload> missing .str_version marker; SkyrimTogetherRuntime/ not installed");
        return false; // payload directory not installed
    }

    // Already deployed? The marker is written into the game root once the mirror below
    // has succeeded, so a stamp naming the same commit means every file it describes is
    // already in place and the scan can be skipped entirely. See SameBuild for what
    // happens when either side carries no stamp.
    //
    // This is worth doing rather than trusting the per-file timestamp check: that
    // check still costs several stat calls per file, and under Mod Organizer every
    // one of them crosses the USVFS hook. It also cannot help when the timestamp
    // could not be advanced (see DeployFile), which is exactly the case where the
    // payload gets compared byte-for-byte on every single launch.
    //
    // The one thing the marker cannot vouch for is a file that was removed or
    // quarantined after the deploy: the payload would be reported as current and the
    // launch would then fail inside LoadLibraryW, which surfaces as "Windows refused"
    // rather than as a missing file. Two stats on the client dlls are cheap next to
    // the scan this saves.
    const auto clientDllsPresent = [&acGameRoot]() {
        std::error_code dllEc;
        return std::filesystem::exists(acGameRoot / kClientDllName, dllEc) ||
               std::filesystem::exists(acGameRoot / kClientDllLegacyName, dllEc);
    };

    std::error_code markerEc;
    const auto deployedMarker = acGameRoot / kVersionMarker;
    if (std::filesystem::exists(deployedMarker, markerEc) && SameBuild(marker, deployedMarker) && clientDllsPresent())
    {
        // Worth one line: without it the only evidence of the fast path is the
        // absence of the per-file walk, which is exactly what a user reporting a
        // stale runtime needs to be able to rule out. An unstamped payload matches
        // by content, so the line names the commit only when there is one.
        std::string commit, branch;
        ReadBuildStamp(marker, commit, branch);

        const std::wstring stamp(commit.begin(), commit.end());
        AppendLog(acGameRoot / L"st_boot.log",
                  L"[deploy] payload already current (" + (stamp.empty() ? L"unstamped" : stamp) +
                      L"), skipping the per-file scan");
        return true;
    }

    // walk the payload and mirror it into the game root
    //
    // One error_code per call. These used to share a single variable: a failure inside
    // is_regular_file left a code behind that the next relative() then saw as its own
    // result, so one unreadable entry could be reported as a path failure and the
    // other way round. Same mistake the comment in DeployFile warns about.
    std::error_code walkEc;
    for (auto it = std::filesystem::recursive_directory_iterator(acPayload, walkEc);
         it != std::filesystem::recursive_directory_iterator(); it.increment(walkEc))
    {
        if (walkEc)
            break;

        std::error_code entryEc;
        const bool isRegular = it->is_regular_file(entryEc);
        if (entryEc || !isRegular)
            continue;

        std::error_code relEc;
        const auto rel = std::filesystem::relative(it->path(), acPayload, relEc);
        if (relEc || rel.empty() || rel.native() == kVersionMarker)
            continue; // the marker itself does not belong into the game root

        const auto target = acGameRoot / rel;

        // A directory that could not be created makes the copy below fail with a
        // misleading code, so it is reported here under its own name instead.
        std::error_code dirEc;
        std::filesystem::create_directories(target.parent_path(), dirEc);
        if (dirEc)
        {
            wchar_t buf[320];
            swprintf_s(buf, L"failed: cannot create %s (error %d)", target.parent_path().c_str(), dirEc.value());
            aFailures.emplace_back(buf);
            continue;
        }

        DWORD error = ERROR_SUCCESS;
        if (!DeployFile(it->path(), target, error))
        {
            wchar_t buf[320];
            swprintf_s(buf, L"failed: %s (error %lu)", rel.c_str(), error);
            aFailures.emplace_back(buf);
        }
    }

    // Only once every file landed, so a partial deploy is never recorded as
    // complete. A failure here is not fatal: the next launch simply repeats the
    // per-file scan it would have repeated anyway.
    if (aFailures.empty())
    {
        std::error_code writeEc;
        std::filesystem::copy_file(marker, deployedMarker,
                                   std::filesystem::copy_options::overwrite_existing, writeEc);
    }

    return aFailures.empty();
}

void ShowDeployError(const std::filesystem::path& acGameRoot, const std::filesystem::path& acPayload,
                     const std::vector<std::wstring>& aFailures)
{
    const bool payloadMissing = !std::filesystem::exists(acPayload);
    const bool underMo2 = IsVirtualFileSystemActive();

    // write the detailed list to the game root so the user can report it
    const auto log = acGameRoot / L"st_deploy_error.log";
    AppendLog(log, L"[deploy] game root: " + acGameRoot.wstring());
    AppendLog(log, L"[deploy] payload: " + acPayload.wstring());
    AppendLog(log, L"[deploy] this plugin was loaded from: " + SelfPath());
    AppendLog(log, std::wstring(L"[deploy] mod organizer virtual file system: ") +
                       (underMo2 ? L"active" : L"NOT active"));
    AppendLog(log, L"[deploy] Data contains: " + DescribeDataFolder(acGameRoot));
    for (const auto& failure : aFailures)
        AppendLog(log, L"[deploy] " + failure);

    // simple MessageBox based report; task dialogs need comctl32 linkage we
    // deliberately avoid in this bootstrap dll. Only the first few failures
    // are quoted so the message stays readable.
    std::wstring detail;
    const size_t shown = aFailures.size() > 3 ? 3 : aFailures.size();
    for (size_t i = 0; i < shown; i++)
        detail += L"\n  " + aFailures[i];
    if (aFailures.size() > shown)
        detail += L"\n  ... and " + std::to_wstring(aFailures.size() - shown) + L" more (see st_deploy_error.log)";

    // A missing payload and a payload that would not copy are different
    // problems with different fixes, and telling everyone to copy files by
    // hand sends the common case down the wrong path.
    std::wstring advice;
    if (payloadMissing)
    {
        advice = L"\n\nThe mod's SkyrimTogetherRuntime folder is not visible to the game, so there is "
                 L"nothing to deploy. This is not a conflict with SKSE or with other SKSE mods.\n\n";
        advice += underMo2
                      ? L"Mod Organizer is managing this game, so check that the Skyrim Together mod is "
                        L"ticked in the left pane and that it contains a SkyrimTogetherRuntime folder. "
                        L"If this plugin was loaded from the game folder rather than from the mod (see "
                        L"st_deploy_error.log), delete that stray copy of SkyrimTogetherSKSE.dll - it "
                        L"runs even when the mod is disabled."
                      : L"The game was not started through Mod Organizer, so only files that really sit "
                        L"in Data are visible. Either start the game through Mod Organizer, or install "
                        L"the mod by extracting it into Data directly.";
    }
    else
    {
        advice = L"\n\nPlease copy the contents of the payload folder into the game root "
                 L"manually (libcef.dll and friends), or run the game as administrator once.";
    }

    const std::wstring msg = L"Skyrim Together could not deploy its runtime files.\n\n"
                             L"Payload: " + acPayload.wstring() +
                             L"\nGame root: " + acGameRoot.wstring() +
                             detail + advice;
    MessageBoxW(nullptr, msg.c_str(), L"Skyrim Together", MB_ICONERROR | MB_OK);
}

bool StartClient(const std::filesystem::path& acGameRoot)
{
    const bool legacy = IsLegacyGame();
    const auto clientPath = acGameRoot / (legacy ? kClientDllLegacyName : kClientDllName);

    // Records which runtime was picked before trying to load it. Without this
    // line a plugin that SKSE never loaded and a plugin that loaded but chose
    // the wrong dll look identical from the outside.
    AppendLog(acGameRoot / L"st_boot.log", std::wstring(L"[bootstrap] game version is ") +
                                              (legacy ? L"1.5.x, loading " : L"1.6.x/1.7.x, loading ") +
                                              clientPath.wstring());

    HMODULE h = LoadLibraryW(clientPath.c_str());
    if (!h)
    {
        const DWORD error = GetLastError();

        // diagnostic marker so a failure to start the client is not silent:
        // which dll was picked, its size on disk (a truncated copy from an
        // interrupted deploy fails with error 182) and the loader error
        const auto marker = acGameRoot / L"st_client_error.log";
        wchar_t buf[192];
        std::error_code ec;
        const auto fileSize = std::filesystem::file_size(clientPath, ec);
        swprintf_s(buf, L"selected=%s size=%llu error=%lu",
                   legacy ? kClientDllLegacyName : kClientDllName,
                   ec ? 0ull : static_cast<unsigned long long>(fileSize),
                   error);
        AppendLog(marker, buf);

        // the client loads libcef.dll (+ chrome_elf.dll) at load time, so
        // a truncated copy of any of these in the game root breaks the
        // client with error 182 too; record their on-disk sizes as well
        const wchar_t* kDeps[] = {L"libcef.dll", L"chrome_elf.dll",
                                  L"d3dcompiler_47.dll", L"TPProcess.exe"};
        for (const wchar_t* dep : kDeps)
        {
            const auto depPath = acGameRoot / dep;
            const auto depSize = std::filesystem::file_size(depPath, ec);
            wchar_t depBuf[160];
            swprintf_s(depBuf, L"dep %s size=%llu", dep,
                       ec ? 0ull : static_cast<unsigned long long>(depSize));
            AppendLog(marker, depBuf);
        }

        // Without this the failure is invisible in game: SKSE ignores a plugin
        // that returns false, the game keeps running, and the only symptom is
        // that the multiplayer menu never opens. Say so instead.
        std::wstring msg = L"Skyrim Together could not start.\n\nIt tried to load\n  " + clientPath.wstring() +
                           L"\nbut Windows refused with error " + std::to_wstring(error) + L".";
        if (error == ERROR_MOD_NOT_FOUND || error == ERROR_FILE_NOT_FOUND)
        {
            msg += legacy ? L"\n\nThis build does not contain the 1.5.x runtime "
                            L"(SkyrimTogetherRuntime_1_5.dll). Your game is version 1.5.x, which needs it - "
                            L"please download a mod package that ships it."
                          : L"\n\nThe runtime was not deployed into the game folder.";
        }
        msg += L"\n\nDetails were written to st_client_error.log in the game folder.";
        MessageBoxW(nullptr, msg.c_str(), L"Skyrim Together", MB_ICONERROR | MB_OK);

        return false;
    }

    using BootstrapFn = bool (*)(const wchar_t*);
    const auto bootstrap = reinterpret_cast<BootstrapFn>(GetProcAddress(h, kClientEntry));
    if (!bootstrap)
    {
        AppendLog(acGameRoot / L"st_client_error.log", std::wstring(L"loaded ") + clientPath.filename().wstring() +
                                                          L" but it has no STClient_Bootstrap export");
        return false;
    }

    return bootstrap(acGameRoot.c_str());
}
} // namespace

extern "C" {
// The export SKSE 2.2.x requires before it will load this dll at all. See the
// comment on SksePluginVersionData for why its absence is silent, and for why
// this declares version independence instead of listing runtimes.
//
// compatibleVersions stays empty: SKSE only reads it when the plugin is not
// version independent, and leaving it zeroed keeps the struct identical to the
// one CommonLibSSE generates for the companion plugins.
__declspec(dllexport) SksePluginVersionData SKSEPlugin_Version = {
    SksePluginVersionData::kVersion,
    1, // pluginVersion, matching SKSEPlugin_Query's info->version
    "SkyrimTogether",
    "",
    "",
    SksePluginVersionData::kVersionIndependentEx_NoStructUse,
    SksePluginVersionData::kVersionIndependent_Signatures,
    {},
    0,
};

// The two flags are the whole fix: without either one SKSE rejects the entry on
// 1.6.629+ with "only compatible with versions earlier than 1.6.629", and
// without the Signatures flag it fails on 1.7.99+ demanding the V5 address
// library. Both failures look identical from the outside - no F2 menu - so the
// values are pinned here rather than left to a log line. Asserted on the named
// constants, because an exported variable is not a constant expression.
constexpr auto kDeclaredIndependence = SksePluginVersionData::kVersionIndependent_Signatures;
constexpr auto kDeclaredIndependenceEx = SksePluginVersionData::kVersionIndependentEx_NoStructUse;
static_assert(kDeclaredIndependence & 0b010,
              "must claim Signatures, or 1.7.99+ rejects the plugin");
static_assert(kDeclaredIndependenceEx & 0b001,
              "must claim NoStructUse, or 1.6.629+ rejects the plugin");
static_assert(SksePluginVersionData::kVersion == 1, "SKSE requires dataVersion 1");

__declspec(dllexport) bool SKSEPlugin_Query(const void*, SksePluginInfo* apInfo)
{
    if (!apInfo)
        return false;
    apInfo->infoVersion = kSksePluginInfoVersion;
    apInfo->name = "SkyrimTogether";
    apInfo->version = 1;
    return true;
}

__declspec(dllexport) bool SKSEPlugin_Load(const void*)
{
    _setmaxstdio(8192);

    const auto gameRoot = GetGameRoot();
    if (gameRoot.empty())
        return false;

    // Recorded every launch, because it answers the two questions every
    // "nothing happened" report turns out to hinge on: whether Mod Organizer
    // is actually managing this process, and whether this plugin came from the
    // mod or from a stray copy left in the game folder.
    AppendLog(gameRoot / L"st_boot.log", L"[bootstrap] loaded from " + SelfPath() +
                                             (IsVirtualFileSystemActive()
                                                  ? L", mod organizer vfs active"
                                                  : L", mod organizer vfs NOT active"));

    // Data/<RuntimeDir> - MO2 virtualizes this when installed as a mod; a
    // manual Data copy resolves to the same location
    const auto payload = gameRoot / L"Data" / kRuntimeDirName;
    std::vector<std::wstring> failures;
    if (!DeployRuntime(gameRoot, payload, failures))
    {
        // payload missing entirely -> nothing we can do automatically
        if (!std::filesystem::exists(payload))
        {
            ShowDeployError(gameRoot, payload, failures);
            return false;
        }
        // some files failed to copy; only nag when a critical file is among
        // them (the client dlls or the cef runtime), otherwise the client
        // may still start and the failure is logged in st_deploy_error.log
        bool critical = false;
        for (const auto& failure : failures)
        {
            if (failure.find(L"SkyrimTogetherRuntime") != std::wstring::npos ||
                failure.find(L"libcef") != std::wstring::npos)
            {
                critical = true;
                break;
            }
        }
        if (critical)
            ShowDeployError(gameRoot, payload, failures);
        else
        {
            // still record what failed so it is diagnosable, but keep going
            // - the client may start if the failed file is not critical
            const auto log = gameRoot / L"st_deploy_error.log";
            AppendLog(log, L"[deploy] non-critical failures; continuing");
            for (const auto& failure : failures)
                AppendLog(log, L"[deploy] " + failure);
        }
    }

    return StartClient(gameRoot);
}
}
