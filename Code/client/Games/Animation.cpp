#include <TiltedOnlinePCH.h>

#include <Games/Overrides.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <Forms/TESIdleForm.h>

#include <Structs/ActionEvent.h>

#include <Games/Animation/ActorMediator.h>
#include <Games/Animation/TESActionData.h>

#include <Misc/BSFixedString.h>

#include <World.h>

TP_THIS_FUNCTION(TPerformAction, uint8_t, ActorMediator, TESActionData* apAction);
static TPerformAction* RealPerformAction;


static uint8_t TP_MAKE_THISCALL(HookPerformAction, ActorMediator, TESActionData* apAction)
{
    auto pActor = apAction->actor;
    const auto pExtension = pActor ? pActor->GetExtension() : nullptr;

    // Skyrim's action processing is recursive
    // We only want to capture the initial entry and some post-execution data
    const bool bIsInitialEntry = !ScopedActionProcessOverride::IsOverriden();
    ScopedActionProcessOverride performActionOverride;

    if (bIsInitialEntry && !pActor)
        spdlog::warn("Action {} has no actor", apAction->action->formID);

    const bool bDoNetworkSend = pExtension && !pExtension->IsRemote() && bIsInitialEntry;
    const bool bSTRInitialEntry = bIsInitialEntry && (apAction->someFlag & BGSActionData::kSTRControlled) != 0;
    const bool bG_ForcedInitialEntry = bIsInitialEntry && ScopedForceAnimationOverride::IsOverriden();
    
    if (bDoNetworkSend || bSTRInitialEntry || bG_ForcedInitialEntry)
    {
        ActionEvent Event;

        if (pActor)
        {
            BSScopedLock actorLock(pActor->actorLock);
            Event.ActorId = pActor->formID;
            Event.State1 = pActor->actorState.flags1;
            Event.State2 = pActor->actorState.flags2;
            pActor->SaveAnimationVariables(Event.Variables);
        }
        
        Event.Type = apAction->unkInput | (apAction->someFlag ? 0x4 : 0);
        Event.Tick = World::Get().GetTick();
        Event.ActionId = apAction->action ? apAction->action->formID : 0;
        Event.TargetId = apAction->target ? apAction->target->formID : 0;

        

        // Important note: "spammed" actions with no name were because of how actions are recursively processed
        const auto res = TiltedPhoques::ThisCall(RealPerformAction, apThis, apAction);
        
        if (res)
        {
            // TODO: Is it beneficial to send sequence or sequence and index?
            //      Are they "coupled"?
            Event.EventName = apAction->eventName.AsAscii();
            Event.TargetEventName = apAction->targetEventName.AsAscii();
            Event.SequenceId = apAction->sequence ? apAction->sequence->formID : 0;
            Event.IdleId = apAction->idleForm ? apAction->idleForm->formID : 0;
            Event.SequenceIndex = apAction->sequenceIndex;
            
            // Useful even for remote actors for potential ownership transfer edge cases
            if (pExtension)
            {
                pExtension->LatestAnimation = Event;

                // Weapon equip
                // TODO: Investigate the interaction between action process rework and weapon draw special-case
                if(apAction->action->formID == 0x132AF)
                    pExtension->LatestWeapEquipAnimation = Event;
            }

            // TODO: should we send actions from the game where res == 0, as before?
            if (bDoNetworkSend)
                World::Get().GetRunner().Trigger(Event);
        }

        return res;
    }
    
    // Recursive calls should always be allowed but NOT create action events
    // For now, we're simply blocking initial entry from the game
    return bIsInitialEntry ? 0 : TiltedPhoques::ThisCall(RealPerformAction, apThis, apAction);
}

ActorMediator* ActorMediator::Get() noexcept
{
    POINTER_SKYRIMSE(ActorMediator*, s_actorMediator, 403567);

    return *(s_actorMediator.Get());
}

bool ActorMediator::PerformAction(TESActionData* apAction) noexcept
{
    return HookPerformAction(this, apAction);
}

// TODO: Deprecate this?
bool ActorMediator::ForceAction(TESActionData* apAction) noexcept
{
    TP_THIS_FUNCTION(TAnimationStep, uint8_t, ActorMediator, TESActionData*)
    // Comment out because unused, but want to leave the signature here
    // using TApplyAnimationVariables = void*(void*, TESActionData*);
    
    POINTER_SKYRIMSE(TAnimationStep, PerformComplexAction, 38953);

    // ApplyAnimationVariables should not be necessary
    // HookPerformAction allows the recursive calls that do this
    // Haven't deleted it entirely
    // Commented out to have ID for reference
    // POINTER_SKYRIMSE(TApplyAnimationVariables, ApplyAnimationVariables, 39004);
    // POINTER_SKYRIMSE(void*, qword_142F271B8, 403566);

    uint8_t result = 0;

    auto pActor = static_cast<Actor*>(apAction->actor);
    if (!pActor || pActor->animationGraphHolder.IsReady())
    {
        result = TiltedPhoques::ThisCall(PerformComplexAction, this, apAction);

        // ApplyAnimationVariables(*qword_142F271B8.Get(), apAction);
    }

    return result;
}

ActionInput::ActionInput(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
{
    // skip vtable as we never use this directly
    actor = apActor;
    target = apTarget;
    action = apAction;
    unkInput = aParam1;
}

void ActionInput::Release()
{
    actor.Release();
    target.Release();
}

ActionOutput::ActionOutput()
    : eventName("")
    , targetEventName("")
{
    // skip vtable as we never use this directly

    result = 0;
    sequence = nullptr;
    idleForm = nullptr;
    sequenceIndex = 0;
}

void ActionOutput::Release()
{
    eventName.Release();
    targetEventName.Release();
}

BGSActionData::BGSActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
    : ActionInput(aParam1, apActor, apAction, apTarget)
{
    // skip vtable as we never use this directly
    
    someFlag = 0;
}

TESActionData::TESActionData(uint32_t aParam1, Actor* apActor, BGSAction* apAction, TESObjectREFR* apTarget)
    : BGSActionData(aParam1, apActor, apAction, apTarget)
{
    POINTER_SKYRIMSE(void*, s_vtbl, 188603);

    someFlag = 0;

    *reinterpret_cast<void**>(this) = s_vtbl.Get();
}

TESActionData::~TESActionData()
{
    ActionOutput::Release();
    ActionInput::Release();
}

static TiltedPhoques::Initializer s_animationHook(
    []()
    {
        POINTER_SKYRIMSE(TPerformAction, performAction, 38949);

        RealPerformAction = performAction.Get();

        TP_HOOK(&RealPerformAction, HookPerformAction);
    });
