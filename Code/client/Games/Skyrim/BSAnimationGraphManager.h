#pragma once

#include <Games/Primitives.h>

#include <Havok/BShkbAnimationGraph.h>

struct BSFixedString;

struct BSAnimationGraphManager
{
    virtual ~BSAnimationGraphManager();
    virtual void sub_1(void* apUnk1);

    void Release()
    {
        if (InterlockedDecrement(&refCount) == 0)
            this->~BSAnimationGraphManager();
    }

    volatile LONG refCount;
    void* pad_ptrs[6];
    BSTSmallArray<BShkbAnimationGraph> animationGraphs; // 40 - 20
#ifdef SKYRIM_TARGET_LEGACY
    void* pad_ptrs2[8];  // 1.5.x: 8 bytes less (lock@0x98, index@0xA8)
#else
    void* pad_ptrs2[9];
#endif
    BSRecursiveLock lock;  // 98 - 4C (1.5.x) / A0 (1.6.x+)
    void* unkPtrAfterLock; // A0 - 58

#if TP_PLATFORM_32
    void* unkPtrOldrim;
#endif

    uint32_t animationGraphIndex; // A8 - 5C

    SortedMap<uint32_t, String> DumpAnimationVariables(bool aPrintVariables);
    uint64_t GetDescriptorKey(int aForceIndex = -1);
    uint32_t ReSendEvent(BSFixedString* apEventName);
};

#if TP_PLATFORM_64
static_assert(offsetof(BSAnimationGraphManager, animationGraphs) == 0x40);
#ifdef SKYRIM_TARGET_LEGACY
static_assert(offsetof(BSAnimationGraphManager, lock) == 0x98);
static_assert(offsetof(BSAnimationGraphManager, animationGraphIndex) == 0xA8);
#else
static_assert(offsetof(BSAnimationGraphManager, lock) == 0xA0);
static_assert(offsetof(BSAnimationGraphManager, animationGraphIndex) == 0xB0);
#endif
#else
static_assert(offsetof(BSAnimationGraphManager, animationGraphs) == 0x20);
static_assert(offsetof(BSAnimationGraphManager, lock) == 0x4C);
static_assert(offsetof(BSAnimationGraphManager, animationGraphIndex) == 0x5C);
#endif
