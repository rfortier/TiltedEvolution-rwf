#pragma once

#include <optional>
#include <Memory.hpp>
#include <Windows.h>

inline TiltedPhoques::String QueryFileVersion(const wchar_t* acpPath)
{
    // check if viable
    if (DWORD infoSize = GetFileVersionInfoSizeW(acpPath, nullptr))
    {
        auto buffer = TiltedPhoques::MakeUnique<uint8_t[]>(infoSize);
        if (GetFileVersionInfoW(acpPath, 0, infoSize, buffer.get()))
        {
            // Both branches test the same four things, in the same order:
            // the query succeeded, it reported a length, it returned a pointer,
            // and the string it points at is not empty.
            //
            // The ProductVersion branch used to omit that last test while the
            // FileVersion branch right below it had it, so a block that carries
            // the key but leaves it blank returned an empty string and the
            // fallback was never reached. The caller is
            // `if (LC.Version.empty()) DIE_NOW(L"Failed to query game version")`,
            // so that is a launch failure with a message that names the wrong
            // cause - the FileVersion key was there to be read.
            //
            // The other two implementations of this same query both test for an
            // empty string on both keys: client/VersionDb.h and
            // skse_client/main.cpp.
            char* productVersion = nullptr;
            DWORD productVersionLen = 0;

            const char* kKeys[] = {
                "\\StringFileInfo\\040904B0\\ProductVersion",
                "\\StringFileInfo\\040904B0\\FileVersion",
            };

            for (const char* key : kKeys)
            {
                if (VerQueryValueA(buffer.get(), key, reinterpret_cast<LPVOID*>(&productVersion),
                                   reinterpret_cast<PUINT>(&productVersionLen)) &&
                    productVersionLen && productVersion && *productVersion)
                {
                    return productVersion;
                }

                productVersion = nullptr;
                productVersionLen = 0;
            }
        }
    }

    return {};
}
