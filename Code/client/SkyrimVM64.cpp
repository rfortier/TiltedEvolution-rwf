#include <atomic>
#include <cstring>

#include <TiltedOnlinePCH.h>
#include "TiltedOnlineApp.h"
#include <Misc/GameVM.h>

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

struct Main;

TP_THIS_FUNCTION(TVMUpdate, int, GameVM, float);
TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVMDestructor, uintptr_t, void);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

// Counts consecutive vm ticks that reported the vm as inactive. See HookVMUpdate
// for why a long run of them is treated as a broken offset rather than an idle
// game.
static int s_inactiveFrames = 0;

// Tick instrumentation.
//
// Everything else in the client eventually lands in World::Update(), so when a
// session stalls the question is always the same one: did this tick reach us at
// all, and did it decide to run the update. Those two have very different fixes
// and no other evidence tells them apart, because nothing downstream prints
// either way.
static constexpr uint64_t kTickHeartbeatEvery = 600; // roughly ten seconds at 60fps

static std::atomic<uint64_t> s_ticks{0};
static std::atomic<uint64_t> s_updates{0};
static std::atomic<uint64_t> s_skipped{0};
static std::atomic<uint64_t> s_mainLoopTicks{0};

struct RawOffset
{
    bool readable{false};
    int32_t value{};
};

// The two candidate offsets for SkyrimVM::inactive are read even when they are
// not the one this build uses, because the value sitting next door is exactly
// what decides whether the assumed offset is right. Reading past the object has
// to fail quietly rather than take the game down with it.
static RawOffset ReadInt32At(const void* acpBase, size_t aOffset) noexcept
{
    RawOffset probe{};

    __try
    {
        const uint8_t* pAddress = static_cast<const uint8_t*>(acpBase) + aOffset;
        int32_t value = 0;
        memcpy(&value, pAddress, sizeof(value));
        probe.readable = true;
        probe.value = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return probe;
}

static std::string DescribeOffset(const RawOffset& acProbe) noexcept
{
    return acProbe.readable ? std::to_string(acProbe.value) : std::string("<unreadable>");
}

int TP_MAKE_THISCALL(HookVMUpdate, GameVM, float a2)
{
    // inactive is how the game parks the vm (main menu, loading, pause). The
    // runner lives on this tick, so honouring the flag exactly is what makes the
    // client idle outside the game.
    //
    // The cost of being wrong about where the flag sits is not a bad frame, it
    // is a silent, total stop: World::Update() never runs, the task queue is
    // never drained, and every attempt to connect is queued and forgotten while
    // the overlay waits forever on "connecting". That exact failure shipped once
    // because 1.5.97 keeps inactive at 0x680 while the shared struct said 0x690.
    //
    // The flag is therefore treated as untrusted. A run of consecutive frames
    // that all report "inactive" is far past any real menu or load screen, so at
    // that point the tick is let through and the disagreement is logged instead
    // of being allowed to hang the session. A genuine idle state reports the same
    // flag on every frame anyway, so the recovery costs one extra Update() per
    // kInactiveFrameLimit frames and changes nothing else.
    static constexpr int kInactiveFrameLimit = 600;

    const uint64_t cTick = s_ticks.fetch_add(1) + 1;

    // Announce the very first tick and afterwards one in every
    // kTickHeartbeatEvery. Seeing the first and never another means the tick
    // stopped; seeing none means it never started and the hook is elsewhere.
    const bool cAnnounce = cTick == 1 || cTick % kTickHeartbeatEvery == 0;

    const RawOffset cAtLegacy = cAnnounce ? ReadInt32At(apThis, 0x680) : RawOffset{};
    const RawOffset cAtModern = cAnnounce ? ReadInt32At(apThis, 0x690) : RawOffset{};

    if (apThis->inactive == 0)
    {
        s_inactiveFrames = 0;
        ++s_updates;
        g_appInstance->Update();
    }
    else if (++s_inactiveFrames >= kInactiveFrameLimit)
    {
        s_inactiveFrames = 0;

        static bool s_reported = false;
        if (!s_reported)
        {
            s_reported = true;
            spdlog::warn("SkyrimVM::inactive has read non-zero for {} consecutive vm ticks, which no menu or load "
                         "screen lasts; running the client update anyway. The offset this build assumes is {} and the "
                         "real one on this runtime differs, so the field is being read from the wrong place",
                         kInactiveFrameLimit, static_cast<int>(SkyrimVM::kInactiveOffset));
        }

        ++s_updates;
        g_appInstance->Update();
    }
    else
    {
        ++s_skipped;
    }

    if (cAnnounce)
    {
        spdlog::info("vm tick heartbeat: tick {}, client update ran {} times and was gated {} times, this build reads "
                     "inactive at {:#x}, where the value is {}; offset {:#x} holds {} and offset {:#x} holds {}",
                     cTick, s_updates.load(), s_skipped.load(), static_cast<int>(SkyrimVM::kInactiveOffset),
                     apThis->inactive, 0x680, DescribeOffset(cAtLegacy), 0x690, DescribeOffset(cAtModern));
    }

    return TiltedPhoques::ThisCall(VMUpdate, apThis, a2);
}

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
    // Instrumented for the same reason HookVMUpdate is. On 1.5.97 the vm tick
    // has not reached us once in any session logged so far, so if the frame loop
    // is being driven at all it is from somewhere else - and this is the other
    // entry point the client already hooks. Counting both says which one the
    // game actually calls, without either of them changing what it does yet.
    const uint64_t cTick = s_mainLoopTicks.fetch_add(1) + 1;
    if (cTick == 1 || cTick % kTickHeartbeatEvery == 0)
    {
        spdlog::info("main loop heartbeat: tick {}, vm ticks {} so far, client update ran {} times", cTick,
                     s_ticks.load(), s_updates.load());
    }

    return TiltedPhoques::ThisCall(MainLoop, apThis);
}

uintptr_t TP_MAKE_THISCALL(HookVMDestructor, void)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return TiltedPhoques::ThisCall(VMDestructor, apThis);
}

static TiltedPhoques::Initializer s_mainHooks(
    []()
    {
        POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564);
        POINTER_SKYRIMSE(TVMUpdate, cVMUpdate, 53926);
        POINTER_SKYRIMSE(TVMDestructor, cVMDestructor, 40412);

        VMUpdate = cVMUpdate.Get();
        MainLoop = cMainLoop.Get();
        VMDestructor = cVMDestructor.Get();

        TP_HOOK(&VMUpdate, HookVMUpdate);
        TP_HOOK(&MainLoop, HookMainLoop);
        TP_HOOK(&VMDestructor, HookVMDestructor);
    });

