#include <filesystem>
#include <iostream>
#include <fstream>
#include <regex>
#define SPDLOG_WCHAR_FILENAMES
#include <spdlog/formatter.h>

#include "utils/Error.h"        // For Die()
#include "DllBlocklist.h"
#include "../Launcher.h"        // For gameDir
#include "../TargetConfig.h"    // For PRODUCT_NAME

namespace stubs
{
// clang-format off

    // A list of modules blocked for a variety of reasons, but mainly
    // for causing crashes and incompatibility with ST
    const wchar_t* const kDllBlocklist[] = {
        L"EngineFixes.dll",          // Skyrim Engine Fixes, breaks our hooks
        L"SkyrimSoulsRE.dll",        // Our mod implements this with special handling
        L"crashhandler64.dll",       // Stream crash handler, breaks heap
        L"fraps64.dll",              // Breaks tilted ui
        L"SpecialK64.dll",           // breaks rendering
        L"ReShade64_SpecialK64.dll", // same reason
        L"NvCamera64.dll",           // broken af nvidia stuff, blacklisted for now, needs fix later
       // L"atiuxp64.dll",
       // L"aticfx64.dll"
    };

// clang-format on

struct DllGreyEntry
{
    const wchar_t* m_dllName;           // The name of the DLL to check.
    const wchar_t* m_configLocation;    // The relative path of the config file (from the game dir)
    const char*    m_sigRegex;          // If this pattern is found in the config, these routines already fixed it.
    const wchar_t* m_prompt;            // The MessageBox prompt asking for permission to fix.
    const char*    m_sigToInsert;       // This is added at the top of the rewritten config. It can/should say more than just the pattern match signature.
    const char*    m_replacers;         // Regex replacers. Regex & replacement separated by newlines.
};                                      //     You can have more than one replacer, also separated by newlines

const DllGreyEntry kDllGreyList[] = 
{
    {
        L"EngineFixes.dll",
        L"Data\\SKSE\\Plugins\\EngineFixes.toml",
         "# SKYRIM TOGETHER REBORN marker for EngineFixes required compatibility settings v2, DO NOT CHANGE THIS LINE",
 
        L"For EngineFixes to work with Skyrim Together Reborn, some settings are required:\n"
            "\tMemoryManager = false\n"
            "\tScaleformAllocator = false\n"
            "\tMaxStdio = 8192\n\n"

         "OK:\tMakes the changes for you\n"
         "Cancel:\tEngineFixes will not load\n\n"

         "If later you get the (harmless) SrtCrashFix64 popup, manually make this EngineFixes configuration change to suppress it:\n"     
            "\tAnimationLoadSignedCrash = false",



         "# SKYRIM TOGETHER REBORN marker for EngineFixes required compatibility settings v2, DO NOT CHANGE THIS LINE\n"
         "#    MemoryManager = false\n"
         "#    ScaleformAllocator = false\n"
         "#    MaxStdio = 8192\n"
         "#\n"

         "# If you get a SrtCrashFix64 popup, it is because you've loaded a mod like Animation Limit Crash Fixe SSE\n"
         "# that is doing the same thing as EngineFixes. Manually set\n"
         "#    AnimationLoadSignedCrash = false\n"
         "# to eliminate the annoying popup.\n\n",
    
         "(^\\s*MemoryManager\\s*=\\s*)(true ?|false)\n$1false\n"
         "(^\\s*ScaleformAllocator\\s*=\\s*)(true ?|false)\n$1false\n"
         "(^\\s*MaxStdio\\s*=\\s*)([0-9]+)\n$018192\n"       // Only huge builds need this many files, but make EF match what STR sets.
    }
};

enum GreyListDisposition
{
    kNotGreyList,       // Not on the greylist at all
    kGreyListAccept,    // It is on the greylist with a good config, or the changes were accepted
    kGreyListAbort      // It is on the greylist and the configuration is no good.
};


// This is long, lots of error checking, but the comments call out the major steps in the sequence. 
GreyListDisposition IsConfigOK(const std::filesystem::path& aPath, const DllGreyEntry& aEntry)
{
    // Read the entire file into a string buffer.
    std::string newConfig;
    std::filesystem::path configFile = aPath / aEntry.m_configLocation;
    std::fstream file(configFile, std::ios::in);
    std::stringstream configStream;

    if (file.good())
        configStream << file.rdbuf();

    if (configStream.bad() || file.bad())
    {
        auto msg = fmt::format(__FUNCTION__ L": failed to read {}", aEntry.m_configLocation);
        Die(msg.c_str(), true);
        return kGreyListAbort;
    }

    // std::regex throws exceeptions
    try
    {
        // Check for signature regexp, if found then the config file is accepted.
        std::regex regex_pattern(aEntry.m_sigRegex, std::regex_constants::icase);
        if (std::regex_search(configStream.str(), regex_pattern))
            return kGreyListAccept; 

        // Ask user if they want to fix config, or not load the mod.
        if (MessageBoxW(NULL, aEntry.m_prompt, PRODUCT_NAME L" Requires Configuration Changes", MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL)
            return kGreyListAbort;

        // Rewind config file
        file.clear(); // Clear any EOF flags
        file.seekg(0, std::ios::beg);
        newConfig = aEntry.m_sigToInsert;

        // Iterate over each line of the config file
        //     Iterate over each replacer, possibly changing the line
        //     Output upddate line to new config
        std::string line;
        std::stringstream replacers(aEntry.m_replacers);

        while (file.good() && std::getline(file, line))
        {
            std::string pattern;
            std::string withThat;
            while (replacers.good() && std::getline(replacers, pattern) && std::getline(replacers, withThat))
            {
                std::regex replaceThis(pattern, std::regex_constants::icase);

                // Stepping through these two debugging lines is very helpful for finding broken expressions.
                std::smatch matches;
                std::regex_search(line, matches, replaceThis);

                line = std::regex_replace(line, replaceThis, withThat);
            }

            newConfig += line + "\n"; // Save the possibly edited line.
            replacers.clear();
            replacers.seekg(0, std::ios::beg);
        }
    }

    catch ([[maybe_unused]] const std::regex_error& e)
    {
        auto msg = fmt::format(__FUNCTION__ L": invalid regular expression in DLL grey list handling");
        Die(msg.c_str(), true);
        return kGreyListAbort;
    }

    // FINALLY, update the config file.
    // Have to reopen to truncate
    // New configuration in-hand in a string, rewind the config file stream and write it out.
    // !file.bad() because file.eof() SHOULD be true.
    if (!file.bad())
    {
        file.close();
        file = std::fstream(configFile, std::ios::in | std::ios::out | std::ios::trunc);
        file << newConfig;
    }

    if (file.bad())
    {
        auto msg = fmt::format(__FUNCTION__ L": failed to read or rewrite {}", configFile.c_str());
        Die(msg.c_str(), true);
        return kGreyListAbort;
    }

    file.close();
    return kGreyListAccept;
}


// "GreyList" DLLs are ones that can run if they are configured properly.
// It's only applicable to SKSE-loaded add-ons, as far as I can tell.
// They will be in the Data\SKSE\Plugins directory, so we have to know 
// little things like the gamePath. 
// This code runs so early you may not know gamePath yet, 
// So guard against it not being set up.
enum GreyListDisposition IsDllGreyListBlocked(const std::wstring_view aDllName)
{
    GreyListDisposition retval = kNotGreyList;
    launcher::LaunchContext* LC = launcher::GetLaunchContext(); 

    if (!LC)
        return kNotGreyList;

    std::filesystem::path gamePath = LC->gamePath;
    for (auto& greyListEntry : kDllGreyList)
    {
        if (std::wcscmp(aDllName.data(), greyListEntry.m_dllName) == 0)
        {
            // DLL name matches, read in entire config file.
            return IsConfigOK(gamePath, greyListEntry);
        }
    }
    return retval;
}



bool IsDllBlocked(std::wstring_view dllName)
{
    GreyListDisposition retval = IsDllGreyListBlocked(dllName);

    if (retval != kNotGreyList)
        return retval == kGreyListAbort;

    for (const wchar_t* dllEntry : kDllBlocklist)
    {
        if (std::wcscmp(dllName.data(), dllEntry) == 0)
        {
            return true;
        }
    }

    return false;
}
} // namespace stubs
