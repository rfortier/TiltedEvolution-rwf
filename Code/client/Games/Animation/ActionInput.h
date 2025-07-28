#pragma once

#include <Actor.h>

struct BGSAction;
struct BGSAnimationSequencer;

struct ActionInput
{
    ActionInput(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget);

    virtual ~ActionInput() { Release(); } // 00

    virtual ActorState* GetSourceActorState() const { return nullptr; } // 01
    virtual void sub_2() {} // 02
    virtual BGSAnimationSequencer* GetSourceSequencer() const { return nullptr; } // 03

    void Release();

    GamePtr<Actor> actor;          // 8
    GamePtr<TESObjectREFR> target; // 10
    BGSAction* action;             // 18
    uint32_t unkInput;             // 20
};
static_assert(sizeof(ActionInput) == 0x28);
