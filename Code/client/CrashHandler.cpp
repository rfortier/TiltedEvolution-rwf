#include <BranchInfo.h>
#include "CrashHandler.h"
#include <DbgHelp.h>
#include <Windows.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <strsafe.h>

using time_point = std::chrono::system_clock::time_point;

std::string SerializeTimePoint(const time_point& time, const std::string& format)
{
    std::time_t tt = std::chrono::system_clock::to_time_t(time);
    std::tm tm = *std::gmtime(&tt); // GMT (UTC)
    // std::tm tm = *std::localtime(&tt); //Locale time-zone, usually UTC by default.
    std::stringstream ss;
    ss << std::put_time(&tm, format.c_str());
    return ss.str();
}

// "<module>+0x<offset>" for an address that lies inside a loaded module,
// "unmapped" otherwise; lets every logged address be tied back to an image.
void FormatModuleOffset(uintptr_t apAddress, char (&aBuf)[MAX_PATH + 48])
{
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(apAddress), &hMod) &&
        hMod)
    {
        char modName[MAX_PATH] = {};
        if (GetModuleFileNameA(hMod, modName, MAX_PATH))
        {
            const char* base = strrchr(modName, '\\');
            sprintf_s(aBuf, sizeof(aBuf), "%s+0x%llx", base ? base + 1 : modName,
                      apAddress - reinterpret_cast<uintptr_t>(hMod));
            return;
        }
    }
    sprintf_s(aBuf, sizeof(aBuf), "unmapped 0x%llx", apAddress);
}

// Guarded copy so a crash report never faults while reading the crash site.
// Kept free of C++ objects so the __try stays legal under /EHsc.
size_t SafeReadBytes(void* apDst, size_t aLen, uintptr_t apAddress)
{
    size_t read = 0;
    __try
    {
        memcpy(apDst, reinterpret_cast<const void*>(apAddress), aLen);
        read = aLen;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return read;
}

void WriteCrashReport(const EXCEPTION_RECORD* apRecord, const CONTEXT* apContext)
{
    char where[MAX_PATH + 48];
    FormatModuleOffset(reinterpret_cast<uintptr_t>(apRecord->ExceptionAddress), where);
    spdlog::error(__FUNCTION__ ": faulting instruction is in {}", where);

    if (apContext)
    {
        spdlog::error(__FUNCTION__ ": registers: rax={:#x} rbx={:#x} rcx={:#x} rdx={:#x} "
                      "rsi={:#x} rdi={:#x} rbp={:#x} rsp={:#x} r8={:#x} r9={:#x} r10={:#x} "
                      "r11={:#x} r12={:#x} r13={:#x} r14={:#x} r15={:#x} rip={:#x}",
                      apContext->Rax, apContext->Rbx, apContext->Rcx, apContext->Rdx,
                      apContext->Rsi, apContext->Rdi, apContext->Rbp, apContext->Rsp,
                      apContext->R8, apContext->R9, apContext->R10, apContext->R11,
                      apContext->R12, apContext->R13, apContext->R14, apContext->R15,
                      apContext->Rip);
    }

    // An AV carries the kind of access and the address that could not be
    // accessed in ExceptionInformation; without them a bad indirect call
    // and a bad memory write are indistinguishable. Log them.
    if (apRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && apRecord->NumberParameters >= 2)
    {
        const char* access = "?";
        if (apRecord->ExceptionInformation[0] == 0)
            access = "read";
        else if (apRecord->ExceptionInformation[0] == 1)
            access = "write";
        else if (apRecord->ExceptionInformation[0] == 8)
            access = "execute";
        char target[MAX_PATH + 48];
        FormatModuleOffset(apRecord->ExceptionInformation[1], target);
        spdlog::error(__FUNCTION__ ": access type {} (code {}), target address {:#x} ({})", access,
                      apRecord->ExceptionInformation[0], apRecord->ExceptionInformation[1], target);
    }

    // Hexdump around the faulting instruction so the whole function can be
    // matched against the source on this side; the crash is deterministic so
    // this only needs to happen once.
    {
        uint8_t dump[0x400];
        const auto src = reinterpret_cast<uintptr_t>(apRecord->ExceptionAddress) - 0x200;
        const size_t got = SafeReadBytes(dump, sizeof(dump), src);
        char lineBuf[64];
        for (size_t k = 0; k + 16 <= got; k += 16)
        {
            const int relOff = static_cast<int>(k) - 0x200;
            sprintf_s(lineBuf, "%c%05x: ", relOff < 0 ? '-' : '+', relOff < 0 ? -relOff : relOff);
            for (int i = 0; i < 16; i++)
                sprintf_s(lineBuf + 8 + i * 3, sizeof(lineBuf) - (8 + i * 3), "%02x ", dump[k + i]);
            spdlog::error(__FUNCTION__ ": {}", lineBuf);
        }
    }

    // Raw stack walk resolved to "<module>+0x<offset>" so the frames stay
    // readable on a machine without symbols.
    {
        void* frames[24] = {};
        const WORD nFrames = CaptureStackBackTrace(0, sizeof(frames) / sizeof(frames[0]), frames, nullptr);
        for (WORD i = 0; i < nFrames; i++)
        {
            char frameAddr[MAX_PATH + 48];
            FormatModuleOffset(reinterpret_cast<uintptr_t>(frames[i]), frameAddr);
            spdlog::error(__FUNCTION__ ":   #{:02} {}", i, frameAddr);
        }
    }
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    static int alreadyCrashed = 0;
    auto retval = EXCEPTION_CONTINUE_SEARCH;

    // Serialize 
    static std::mutex singleThreaded;
    const std::lock_guard lock{singleThreaded};

    // Check for severe, not continuable and not software-originated exception
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        alreadyCrashed++ == 0)
    {
        spdlog::critical (__FUNCTION__ ": crash occurred!"); 

        spdlog::error(__FUNCTION__ ": exception code is {:x}, at address {}, flags {:x} ",
                      pExceptionInfo->ExceptionRecord->ExceptionCode,
                      pExceptionInfo->ExceptionRecord->ExceptionAddress,
                      pExceptionInfo->ExceptionRecord->ExceptionFlags);

        WriteCrashReport(pExceptionInfo->ExceptionRecord, pExceptionInfo->ContextRecord);

#if (IS_MASTER)
        volatile static bool bMiniDump = false;
#else
        volatile static bool bMiniDump = true;
#endif
        if (bMiniDump)
        {
            HANDLE hDumpFile = NULL;
            try
            {
                MINIDUMP_EXCEPTION_INFORMATION M;
                char dumpPath[MAX_PATH];

                M.ThreadId = GetCurrentThreadId();
                M.ExceptionPointers = pExceptionInfo;
                M.ClientPointers = 0;

                std::ostringstream oss;
                oss << "crash_" << SerializeTimePoint(std::chrono::system_clock::now(), "UTC_%Y-%m-%d_%H-%M-%S")
                    << ".dmp";

                GetModuleFileNameA(NULL, dumpPath, sizeof(dumpPath));
                std::filesystem::path modulePath(dumpPath);
                auto subPath = modulePath.parent_path();

                CrashHandler::RemovePreviousDump(subPath);

                subPath /= oss.str();

                hDumpFile = CreateFileA(subPath.string().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, NULL);

                // baseline settings from https://stackoverflow.com/a/63123214/5273909
                auto dumpSettings = MiniDumpWithDataSegs | MiniDumpWithProcessThreadData | MiniDumpWithHandleData |
                                    MiniDumpWithThreadInfo |
                                    /*
                                    //MiniDumpWithPrivateReadWriteMemory | // this one gens bad dump
                                    MiniDumpWithUnloadedModules |
                                    MiniDumpWithFullMemoryInfo |
                                    MiniDumpWithTokenInformation |
                                    MiniDumpWithPrivateWriteCopyMemory |
                                    */
                                    0;

                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, (MINIDUMP_TYPE)dumpSettings,
                                  (pExceptionInfo) ? &M : NULL, NULL, NULL);
            }
            catch (...) // Mini-dump is best effort only.
            {
            }

            if (!hDumpFile)
                spdlog::critical(__FUNCTION__ ": coredump may have failed.");
            else
            {
                CloseHandle(hDumpFile);
                spdlog::critical(__FUNCTION__ ": coredump created -> flush logs.");
            }
        }

        // Something in STR breaks top-level unhandled exception filters.
        // The Win API for them is pretty clunky (non-atomic, not chainable), 
        // but they can do some important things. If someone actually set one
        // they probably meant it; make sure it actually runs.
        // This will make more CrashLogger mods work with STR.

        // Get the current unhandled exception filter. If it has changed
        // from when STR started up, invoke it here.
        LPTOP_LEVEL_EXCEPTION_FILTER pCurrentUnhandledExceptionFilter = SetUnhandledExceptionFilter(CrashHandler::GetOriginalUnhandledExceptionFilter());
        SetUnhandledExceptionFilter(pCurrentUnhandledExceptionFilter);
        if (pCurrentUnhandledExceptionFilter != CrashHandler::GetOriginalUnhandledExceptionFilter())
        {
            spdlog::critical(__FUNCTION__ ": UnhandledExceptionFilter() workaround triggered.");

            singleThreaded.unlock();        // Might reenter, but is safe at this point.
            if ((*pCurrentUnhandledExceptionFilter)(pExceptionInfo) == EXCEPTION_CONTINUE_EXECUTION)
                retval = EXCEPTION_CONTINUE_EXECUTION;
            singleThreaded.lock();
        }

        spdlog::shutdown();
    }
    return retval;
}

LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::m_pUnhandled;
CrashHandler::CrashHandler()
{
    // Record the original (or as close as we can get) top-level unhandled exception handler.
    // We grab this so we can see if it is changed, presumably by a mod or even graphics drivers.
    // Something in STR breaks unhandled exception handling, so we'll fake it if necessary.
    // This is the only way to get the current setting, but the race is small.
    m_pUnhandled = SetUnhandledExceptionFilter(NULL);
    SetUnhandledExceptionFilter(m_pUnhandled);

    m_handler = AddVectoredExceptionHandler(1, &VectoredExceptionHandler);
}

CrashHandler::~CrashHandler()
{
}

void CrashHandler::RemovePreviousDump(std::filesystem::path path)
{
    for (auto& entry : std::filesystem::directory_iterator(path))
    {
        if (entry.path().string().find("crash") != std::string::npos)
        {
            DeleteFileA(entry.path().string().c_str());
        }
    }
}
