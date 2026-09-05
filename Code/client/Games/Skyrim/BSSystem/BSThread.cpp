
#include <Games/Skyrim/BSSystem/BSThread.h>
#include <base/threading/ThreadUtils.h>

#include <Games/GamePatch.h>

namespace
{
void (*BSThread_Initialize)(BSThread*, int, const char*){nullptr};

void Hook_BSThread_Initialize(BSThread* apThis, int aStackSize, const char* apName)
{
    BSThread_Initialize(apThis, aStackSize, apName);

    if (!apName && apThis->m_ThreadHandle)
        spdlog::warn("Unnamed thread started: {}", apThis->m_ThreadID);

    // this means the thread was successfully created
    if (apThis->m_ThreadHandle)
    {
        bool result = Base::SetThreadName(apThis->m_ThreadHandle, apName);
        if (!result)
        {
            spdlog::warn("Failed to set thread name for tid {} to {}", apThis->m_ThreadID, apName);
        }
    }
}

// bsthreadutils
// hook this in order to redirect the game to use the new naming apis (windows 10+)
void Hook_SetThreadName(uint32_t aThreadId, const char* apThreadName)
{
    // query thread handle
    if (auto hThread = ::OpenThread(THREAD_QUERY_INFORMATION, FALSE, aThreadId))
    {
        Base::SetThreadName(hThread, apThreadName);

        ::CloseHandle(hThread);
    }
    else
        spdlog::warn("Hook_SetThreadName(): Unable to query thread handle :(");
}

// experimental stuff..
#if 0
typedef struct
{
    DWORD dwType;     // Must be 0x1000.
    LPCSTR szName;    // Pointer to name (in user addr space).
    DWORD dwThreadID; // Thread ID (-1=caller thread).
    DWORD dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;

void Hook_RaiseException(DWORD, DWORD, DWORD, const THREADNAME_INFO* lpArguments)
{
    base::SetCurrentThreadName(lpArguments->szName);
}

HANDLE WINAPI Hook_CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
                                LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags,
                                LPDWORD lpThreadId)
{
    auto hThread =
        CreateThread(lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);

    if (hThread && lpThreadId)
    {
        auto name = fmt::format("BNetThread{}", *lpThreadId);
        base::SetThreadName(hThread, name.c_str());
    }

    return hThread;
}
#endif
} // namespace

static TiltedPhoques::Initializer s_BSThreadInit(
    []()
    {
        // an unmapped id resolves to the shared unresolved stub, and detouring
        // or overwriting that stub redirects every other unresolved call in
        // the client into these hooks - so both sites need a real address.
        if (auto* pThreadInit = GamePatch::Anchor(68261, "thread names"))
        {
            BSThread_Initialize = reinterpret_cast<decltype(BSThread_Initialize)>(pThreadInit);
            // need to detour this for now :/
            TP_HOOK_IMMEDIATE(&BSThread_Initialize, &Hook_BSThread_Initialize);
        }

        if (auto* pSetThreadName = GamePatch::Anchor(69066, "thread naming api"))
            GamePatch::Jump(pSetThreadName, &Hook_SetThreadName, "thread naming api");

#if 0
    // relatively safe to do, since this is unlikely to ever change, as beth wont update havok
    if (auto* pCreateHavokThread = GamePatch::Anchor(57704, "havok thread names"))
    {
        GamePatch::Nop(pCreateHavokThread + 0x81, 6, "havok thread names");
        GamePatch::PutCall(pCreateHavokThread + 0x81, &Hook_RaiseException, "havok thread names");
    }

    // Hook_CreateThread went here; it needs an address library id for the
    // CreateThread call site (it used to be a hardcoded 1.6.x VA).
#endif
    });
