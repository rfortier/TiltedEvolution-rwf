#pragma once

#include <Misc/BSFixedString.h>

struct TESIdleForm;

struct ActionOutput
{
    ActionOutput();
    ~ActionOutput() { Release(); }

    void Release();

    // TODO: Do we need to serialize ActionOutput at all?
    
    BSFixedString eventName;       // 00 - 28
    BSFixedString targetEventName; // 08 - 30
    int result;                    // 10 - 38
    TESIdleForm* sequence;         // 18 - 40
    TESIdleForm* idleForm;         // 20 - 48
    uint32_t sequenceIndex;        // 28 - 50
};
static_assert(sizeof(ActionOutput) == 0x30);
