#pragma once

#include <Misc/BSFixedString.h>
#include <BSCore/BSTHashMap.h>

struct BGSActionData;

struct BGSAnimationSequencer
{
    uint32_t numSequences;
    uint32_t pad04;
    creation::BSTHashMap<BSFixedString, BGSActionData*> actions;
};
static_assert(sizeof(BGSAnimationSequencer) == 0x38);
