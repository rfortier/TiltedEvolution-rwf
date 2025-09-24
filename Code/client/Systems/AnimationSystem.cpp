#include <TiltedOnlinePCH.h>

#include <Systems/AnimationSystem.h>

#include <Games/Animation/TESActionData.h>
#include <Games/Animation/ActorMediator.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>

#include <Messages/ClientReferencesMoveRequest.h>

#include <Components.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

extern thread_local const char* g_animErrorCode;

void AnimationSystem::Update(World& aWorld, Actor* apActor, RemoteAnimationComponent& aAnimationComponent,
                             const uint64_t aTick) noexcept
{
    auto& actions = aAnimationComponent.TimePoints;

    if (actions.empty() || !apActor->animationGraphHolder.IsReady())
        return;

    auto it = actions.begin();

    // Process multiple actions per update
    // Currently testing without any interpolation
    while (it != actions.end()) // && it->Tick <= aTick)
    {
        if (!apActor->animationGraphHolder.IsReady())
            break;

        const auto pAction = Cast<BGSAction>(TESForm::GetById(it->ActionId));
        const auto pTarget = Cast<TESObjectREFR>(TESForm::GetById(it->TargetId));
        
        // Load actor state flags (primarily movement state)
        apActor->actorState.flags1 = it->State1;
        apActor->actorState.flags2 = it->State2;

        // Action system functions without this in most if not all vanilla cases
        // Maybe there was originally a reason why?
        // Mod compatibility may depend on this now, though
        apActor->LoadAnimationVariables(it->Variables);

        // TODO: copy after the end of batch processing,
        //       if multiple actions are processed?
        aAnimationComponent.LastRanAction = *it;

        // Corresponds to unkInput
        uint32_t unkInput = it->Type & 0x3;
        
        // Play the animation
        TESActionData actionData(unkInput, apActor, pAction, pTarget);
        actionData.eventName = BSFixedString(it->EventName.c_str());
        actionData.idleForm = Cast<TESIdleForm>(TESForm::GetById(it->IdleId));
        actionData.someFlag = ((it->Type & 0x4) != 0) ? 1 : 0;

        // Mark this as an STR-controlled action
        // This is needed to allow the game's recursive processing calls
        actionData.someFlag |= BGSActionData::kSTRControlled;
        
        // TODO: not sure if we should use PerformAction or ForceAction
        //const auto result = ActorMediator::Get()->PerformAction(&actionData);
        const auto result = ActorMediator::Get()->ForceAction(&actionData);
        
        if (result)
            spdlog::info("Action {} processed with result: {}", it->EventName, result);
        else
            spdlog::warn("Action {} failed with result: {}", it->EventName, result);

        ++it;
    }

    // Remove all processed actions
    actions.erase(actions.begin(), it);
}

void AnimationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<RemoteAnimationComponent>(aEntity))
        aWorld.remove<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::AddAction(RemoteAnimationComponent& aAnimationComponent, const std::string& acActionDiff) noexcept
{
    auto& lastProcessedAction = aAnimationComponent.LastProcessedAction;

    TiltedPhoques::ViewBuffer buffer((uint8_t*)acActionDiff.data(), acActionDiff.size());
    Buffer::Reader reader(&buffer);

    lastProcessedAction.ApplyDifferential(reader);

    aAnimationComponent.TimePoints.push_back(lastProcessedAction);
}

void AnimationSystem::Serialize(World& aWorld, ClientReferencesMoveRequest& aMovementSnapshot, LocalComponent& localComponent, LocalAnimationComponent& animationComponent, FormIdComponent& formIdComponent)
{
    const auto pForm = TESForm::GetById(formIdComponent.Id);
    const auto pActor = Cast<Actor>(pForm);
    if (!pActor)
        return;

    auto& update = aMovementSnapshot.Updates[localComponent.Id];
    auto& movement = update.UpdatedMovement;

    if (const auto pCell = pActor->parentCell)
        World::Get().GetModSystem().GetServerModId(pCell->formID, movement.CellId.ModId, movement.CellId.BaseId);

    if (const auto pWorldSpace = pActor->GetWorldSpace())
        World::Get().GetModSystem().GetServerModId(pWorldSpace->formID, movement.WorldSpaceId.ModId, movement.WorldSpaceId.BaseId);

    movement.Position = pActor->position;

    movement.Rotation.x = pActor->rotation.x;
    movement.Rotation.y = pActor->rotation.z;

    pActor->SaveAnimationVariables(movement.Variables);

    if (pActor->currentProcess && pActor->currentProcess->middleProcess)
    {
        movement.Direction = pActor->currentProcess->middleProcess->direction;
    }

    for (auto& entry : animationComponent.Actions)
    {
        update.ActionEvents.push_back(entry);
    }

    auto latestAction = animationComponent.GetLatestAction();

    if (latestAction)
        localComponent.CurrentAction = latestAction.MoveResult();

    animationComponent.Actions.clear();
}

bool AnimationSystem::Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData)
{
    uint32_t actionBaseId = 0;
    uint32_t actionModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.ActionId, actionModId, actionBaseId))
        return false;

    uint32_t targetBaseId = 0;
    uint32_t targetModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.TargetId, targetModId, targetBaseId))
        return false;

    uint8_t scratch[1 << 14];
    TiltedPhoques::ViewBuffer buffer(scratch, std::size(scratch));
    Buffer::Writer writer(&buffer);
    aActionEvent.GenerateDifferential(aLastProcessedAction, writer);

    apData->assign(buffer.GetData(), buffer.GetData() + writer.Size());

    return true;
}
