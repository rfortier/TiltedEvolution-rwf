#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <ShlObj.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

inline errno_t STRPMDiagnosticRedirectFopen(
    FILE** file,
    const char* filename,
    const char* mode) noexcept
{
    constexpr char kLegacyDiagnosticLogPath[] =
        "Data\\SKSE\\Plugins\\STRPluginMessagingDiagnostic.log";

    if (file != nullptr &&
        filename != nullptr &&
        std::strcmp(filename, kLegacyDiagnosticLogPath) == 0)
    {
        PWSTR documents = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(
                FOLDERID_Documents,
                KF_FLAG_DEFAULT,
                nullptr,
                &documents)) &&
            documents != nullptr)
        {
            std::filesystem::path logDirectory(documents);
            CoTaskMemFree(documents);

            logDirectory /= L"My Games";
            logDirectory /= L"Skyrim Special Edition";
            logDirectory /= L"SKSE";

            std::error_code error;
            std::filesystem::create_directories(logDirectory, error);
            if (!error)
            {
                const auto logPath = logDirectory / L"STRPluginMessagingDiagnostic.log";

                wchar_t wideMode[8]{};
                if (mode != nullptr)
                {
                    std::size_t index = 0;
                    for (; mode[index] != '\0' && index + 1 < std::size(wideMode); ++index)
                        wideMode[index] = static_cast<wchar_t>(static_cast<unsigned char>(mode[index]));
                    wideMode[index] = L'\0';
                }
                if (wideMode[0] == L'\0')
                {
                    wideMode[0] = L'a';
                    wideMode[1] = L'\0';
                }

                const auto result = _wfopen_s(file, logPath.c_str(), wideMode);
                if (result == 0)
                    return 0;
            }
        }
    }

    return ::fopen_s(file, filename, mode);
}

#define fopen_s STRPMDiagnosticRedirectFopen
