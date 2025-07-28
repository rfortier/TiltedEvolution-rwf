#pragma once

#include <Games/Animation/ActionInput.h>
#include <Games/Animation/ActionOutput.h>

struct BGSActionData : ActionInput, ActionOutput
{
    enum SomeFlag : uint32_t
    {
        kDoAnimation = 0,
        kTransitionNoAnimation = 1 << 0,
        kSkip = 1 << 1,
        kSTRControlled = 1 << 30
    };

    BGSActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget);
    ~BGSActionData() override {}

    virtual BGSActionData* Clone() { return nullptr; } // 04
    virtual bool Perform() { return false; } // 05

    uint32_t someFlag;
    // 4 padding bytes
};
static_assert(sizeof(BGSActionData) == 0x60);
