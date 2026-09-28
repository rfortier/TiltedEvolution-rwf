#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cwchar>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace STRPMRuntimeLoad
{
    inline HMODULE LoadLibraryAdjacentToBroker(LPCWSTR modulePath) noexcept
    {
        if (modulePath == nullptr || modulePath[0] == L'\0') {
            return nullptr;
        }

        const bool hasDirectory =
            std::wcschr(modulePath, L'\\') != nullptr ||
            std::wcschr(modulePath, L'/') != nullptr ||
            (std::wcslen(modulePath) >= 2 && modulePath[1] == L':');

        if (!hasDirectory) {
            wchar_t brokerPath[32768]{};
            const auto length = GetModuleFileNameW(
                reinterpret_cast<HMODULE>(&__ImageBase),
                brokerPath,
                static_cast<DWORD>(std::size(brokerPath)));

            if (length > 0 && length < std::size(brokerPath)) {
                std::wstring adjacentPath(brokerPath, length);
                const auto separator = adjacentPath.find_last_of(L"\\/");
                if (separator != std::wstring::npos) {
                    adjacentPath.resize(separator + 1);
                    adjacentPath += modulePath;
                    if (auto module = ::LoadLibraryW(adjacentPath.c_str()); module != nullptr) {
                        return module;
                    }
                }
            }
        }

        return ::LoadLibraryW(modulePath);
    }
}

// STRPluginMessagingAPIRuntime.cpp uses LoadLibraryW for the optional private
// bridge. Force that bare module-name lookup to prefer the broker DLL's own
// directory, which is Data\SKSE\Plugins in the packaged build. This makes
// startup independent of SKSE plugin enumeration/load order and of the process
// current working directory.
#define LoadLibraryW STRPMRuntimeLoad::LoadLibraryAdjacentToBroker
