
// Immersive Early Loader, idea inspired by chromium https://github.com/chromium/chromium/tree/main/chrome/chrome_elf
#include <Windows.h>

namespace
{
// FARPROC addr = GetProcAddress(ntdll, "wine_get_version");
constexpr wchar_t kUSVFSDllName[] = L"usvfs_x64.dll";

bool g_WasUSVFSActive = false;
} // namespace

#define ELF_EXP __declspec(dllexport)

ELF_EXP bool EarlyInstallSucceeded()
{
    // running under MO2 usvfs is fine, we just report it so the
    // launcher can take the usvfs-specific code paths into account
    return true;
}

ELF_EXP bool WasUSVFSActive()
{
    return g_WasUSVFSActive;
}

#undef ELF_EXP

static void InstallEarlyHooks()
{
}

// create remote thread sentinel

// Warning: The OS loader lock is held during DllMain. Be careful.
// Don't invoke any functions which might trigger LoadLibrary.
// https://msdn.microsoft.com/en-us/library/windows/desktop/dn633971.aspx
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        // the goal here is to check if mo2 usvfs dll was loaded yet.
        g_WasUSVFSActive = GetModuleHandleW(kUSVFSDllName);
        InstallEarlyHooks();
        break;
    }
    case DLL_PROCESS_DETACH: break;
    }
    return TRUE;
}
