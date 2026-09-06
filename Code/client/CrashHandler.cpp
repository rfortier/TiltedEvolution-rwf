#include <BranchInfo.h>
#include "CrashHandler.h"
#include <DbgHelp.h>
#include <Windows.h>
#include <Psapi.h>
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

// The loaded modules with their address ranges. An address that resolves to no
// module can still be placed with this: landing just outside a module points at
// a patched call site or a dead trampoline, while landing nowhere near one
// points at a wild pointer.
void WriteModuleList()
{
    HMODULE mods[512];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
        return;

    const size_t count = needed / sizeof(HMODULE);
    spdlog::error("WriteCrashReport: {} loaded modules", count);
    for (size_t i = 0; i < count && i < 512; i++)
    {
        char name[MAX_PATH] = {};
        MODULEINFO info{};
        if (!GetModuleFileNameA(mods[i], name, MAX_PATH) ||
            !GetModuleInformation(GetCurrentProcess(), mods[i], &info, sizeof(info)))
            continue;
        const char* leaf = strrchr(name, '\\');
        const auto base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
        spdlog::error("  module {:#x}..{:#x} {}", base, base + info.SizeOfImage,
                      leaf ? leaf + 1 : name);
    }
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

// What the memory at an address is, and where it sits relative to the game
// image. The state separates the cases a bare address cannot: free memory is a
// wild pointer or a trampoline that has been released, reserved-not-committed
// is a trampoline whose page was never brought in, committed means the memory
// is there and only the permissions were wrong. The distance from the game
// image is what makes two reports comparable at all - ASLR moves every module
// per run, so an address that keeps the same distance across runs was computed
// from the image, while one that moves with the modules was allocated.
void DescribeAddress(const char* acpWhat, const uintptr_t aAddress)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(aAddress), &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        const char* state = "unknown state";
        if (mbi.State == MEM_FREE)
            state = "free: nothing is allocated there (wild pointer, or a trampoline that was released)";
        else if (mbi.State == MEM_RESERVE)
            state = "reserved but not committed: allocated range, no memory behind it";
        else if (mbi.State == MEM_COMMIT)
            state = "committed";

        spdlog::error(__FUNCTION__ ": {} {:#x} is {}, protect {:#x}, allocation base {:#x}, region size {:#x}",
                      acpWhat, aAddress, state, mbi.Protect,
                      reinterpret_cast<uintptr_t>(mbi.AllocationBase), mbi.RegionSize);
    }

    // A near-null target is a pointer that was never set, not an address with a
    // place in the image, so the distance below would only be noise
    if (aAddress < 0x10000)
    {
        spdlog::error(__FUNCTION__ ": {} {:#x} is a null pointer that was called, not a real address", acpWhat,
                      aAddress);
        return;
    }

    if (const auto gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)))
    {
        const auto delta = static_cast<int64_t>(aAddress) - static_cast<int64_t>(gameBase);
        spdlog::error(__FUNCTION__ ": {} {:#x} is {:#x} bytes {} the game image base {:#x}", acpWhat, aAddress,
                      static_cast<uint64_t>(delta < 0 ? -delta : delta), delta < 0 ? "below" : "above", gameBase);
    }
}

// The call that led into a bad address is the handful of bytes in front of the
// return address, and its form names the owner of the pointer: a call through
// memory has its slot at a fixed place, which can be resolved to a module and
// read back, a direct call means the call site itself was patched, and a call
// through a register means the value was computed earlier and only a dump of
// the caller can say where from. The raw bytes are logged either way so any
// other form can still be decoded by hand.
void DescribeCall(const uintptr_t aReturnAddress)
{
    uint8_t code[16]{};
    if (SafeReadBytes(code, sizeof(code), aReturnAddress - sizeof(code)) != sizeof(code))
        return;

    char bytes[3 * sizeof(code) + 1]{};
    for (size_t i = 0; i < sizeof(code); i++)
        sprintf_s(bytes + i * 3, sizeof(bytes) - i * 3, "%02x ", code[i]);
    spdlog::error(__FUNCTION__ ": the 16 bytes in front of the return address are {}", bytes);

    // call [rip+disp32]: rip is the return address, so the slot is right there
    const uint8_t* pIndirect = code + sizeof(code) - 6;
    if (pIndirect[0] == 0xFF && pIndirect[1] == 0x15)
    {
        int32_t disp = 0;
        memcpy(&disp, pIndirect + 2, sizeof(disp));
        const uintptr_t slot = aReturnAddress + disp;

        char who[MAX_PATH + 48];
        FormatModuleOffset(slot, who);

        uintptr_t held = 0;
        SafeReadBytes(&held, sizeof(held), slot);

        char target[MAX_PATH + 48];
        FormatModuleOffset(held, target);

        spdlog::error(__FUNCTION__ ": called through the pointer at {:#x} ({}), which holds {:#x} ({})", slot, who,
                      held, target);
        return;
    }

    // call rel32: the target is fixed at link time, so it is the entry of the
    // function that was called - dump it, because a hook that redirects a
    // function writes its jump over exactly those bytes
    const uint8_t* pDirect = code + sizeof(code) - 5;
    if (pDirect[0] == 0xE8)
    {
        int32_t disp = 0;
        memcpy(&disp, pDirect + 1, sizeof(disp));
        const uintptr_t called = aReturnAddress + disp;

        char what[MAX_PATH + 48];
        FormatModuleOffset(called, what);

        uint8_t entry[16]{};
        char entryBytes[3 * sizeof(entry) + 1]{};
        if (SafeReadBytes(entry, sizeof(entry), called) == sizeof(entry))
        {
            for (size_t i = 0; i < sizeof(entry); i++)
                sprintf_s(entryBytes + i * 3, sizeof(entryBytes) - i * 3, "%02x ", entry[i]);
        }

        spdlog::error(__FUNCTION__ ": direct call to {:#x} ({}), whose first bytes are {}", called, what, entryBytes);
        DescribeAddress("called address", called);
        return;
    }

    // call r64, with or without a REX prefix
    const uint8_t* pRegister = code + sizeof(code) - 2;
    if (pRegister[0] == 0xFF && (pRegister[1] & 0xF8) == 0xD0)
        spdlog::error(__FUNCTION__ ": called through a register, so the value was loaded earlier and not from a "
                                   "fixed slot");
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

        DescribeAddress("target address", apRecord->ExceptionInformation[1]);

        // An execute fault means the thread jumped somewhere that holds no
        // code, so the faulting address says nothing about who is at fault -
        // the answer is the return address the call pushed, which is still on
        // the stack. Dump the top of the stack with every value that resolves
        // to a module annotated, and the module list to place the rest.
        if (apRecord->ExceptionInformation[0] == 8 && apContext)
        {
            spdlog::error(__FUNCTION__ ": jumped to code that is not there; stack top follows, "
                                      "the first entry inside a module is the caller");
            uintptr_t slots[24]{};
            const size_t got = SafeReadBytes(slots, sizeof(slots), apContext->Rsp);
            bool callDescribed = false;
            for (size_t i = 0; i < got / sizeof(uintptr_t); i++)
            {
                if (slots[i] < 0x10000)
                    continue;
                char who[MAX_PATH + 48];
                FormatModuleOffset(slots[i], who);
                if (strncmp(who, "unmapped", 8) != 0)
                {
                    spdlog::error("  [rsp+{:#03x}] {:#x}  {}", i * sizeof(uintptr_t), slots[i], who);

                    // The first one is the caller, so its call instruction is
                    // the one that went nowhere
                    if (!callDescribed)
                    {
                        DescribeCall(slots[i]);
                        callDescribed = true;
                    }
                }
            }
            WriteModuleList();
        }
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
