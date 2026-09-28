
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
    });
