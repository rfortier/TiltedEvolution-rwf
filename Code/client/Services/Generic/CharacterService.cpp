#include "Forms/TESObjectCELL.h"
#include "Forms/TESWorldSpace.h"
#include "Services/PapyrusService.h"
#include <Services/PartyService.h>

#include <Services/CharacterService.h>
#include <Services/QuestService.h>
#include <Services/TransportService.h>

#include <Games/References.h>
#include <Games/Misc/SubtitleManager.h>

#include <Forms/TESNPC.h>
#include <Interface/UI.h>
#include <Forms/TESQuest.h>

#include <BranchInfo.h>
#include <Components.h>
#include <WireLimits.h>

#include <Systems/InterpolationSystem.h>
#include <Systems/AnimationSystem.h>
#include <Systems/CacheSystem.h>
#include <Systems/FaceGenSystem.h>
#include <Systems/LeveledNpcSystem.h>

#include <Events/ActorAddedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/MountEvent.h>
#include <Events/InitPackageEvent.h>
#include <Events/BeastFormChangeEvent.h>
#include <Events/AddExperienceEvent.h>
#include <Events/DialogueEvent.h>
#include <Events/SubtitleEvent.h>
#include <Events/MoveActorEvent.h>
#include <Events/PartyJoinedEvent.h>

#include <Structs/ActionEvent.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/RequestOwnershipTransfer.h>
#include <Messages/NotifyOwnershipTransfer.h>
#include <Messages/RequestOwnershipClaim.h>
#include <Messages/MountRequest.h>
#include <Messages/NotifyMount.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/SyncExperienceRequest.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/DialogueRequest.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyActorTeleport.h>

#include <World.h>
#include <Games/TES.h>

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_referenceAddedConnection = m_dispatcher.sink<ActorAddedEvent>().connect<&CharacterService::OnActorAdded>(this);
    m_referenceRemovedConnection = m_dispatcher.sink<ActorRemovedEvent>().connect<&CharacterService::OnActorRemoved>(this);

    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&CharacterService::OnUpdate>(this);
    m_actionConnection = m_dispatcher.sink<ActionEvent>().connect<&CharacterService::OnActionEvent>(this);

    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&CharacterService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&CharacterService::OnDisconnected>(this);

    m_assignCharacterConnection = m_dispatcher.sink<AssignCharacterResponse>().connect<&CharacterService::OnAssignCharacter>(this);
    m_characterSpawnConnection = m_dispatcher.sink<CharacterSpawnRequest>().connect<&CharacterService::OnCharacterSpawn>(this);
    m_referenceMovementSnapshotConnection = m_dispatcher.sink<ServerReferencesMoveRequest>().connect<&CharacterService::OnReferencesMoveRequest>(this);
    m_factionsConnection = m_dispatcher.sink<NotifyFactionsChanges>().connect<&CharacterService::OnFactionsChanges>(this);
    m_ownershipTransferConnection = m_dispatcher.sink<NotifyOwnershipTransfer>().connect<&CharacterService::OnOwnershipTransfer>(this);
    m_removeCharacterConnection = m_dispatcher.sink<NotifyRemoveCharacter>().connect<&CharacterService::OnRemoveCharacter>(this);

    m_mountConnection = m_dispatcher.sink<MountEvent>().connect<&CharacterService::OnMountEvent>(this);
    m_notifyMountConnection = m_dispatcher.sink<NotifyMount>().connect<&CharacterService::OnNotifyMount>(this);

    m_initPackageConnection = m_dispatcher.sink<InitPackageEvent>().connect<&CharacterService::OnInitPackageEvent>(this);
    m_newPackageConnection = m_dispatcher.sink<NotifyNewPackage>().connect<&CharacterService::OnNotifyNewPackage>(this);

    m_notifyRespawnConnection = m_dispatcher.sink<NotifyRespawn>().connect<&CharacterService::OnNotifyRespawn>(this);
    m_beastFormChangeConnection = m_dispatcher.sink<BeastFormChangeEvent>().connect<&CharacterService::OnBeastFormChange>(this);

    m_addExperienceEventConnection = m_dispatcher.sink<AddExperienceEvent>().connect<&CharacterService::OnAddExperienceEvent>(this);
    m_syncExperienceConnection = m_dispatcher.sink<NotifySyncExperience>().connect<&CharacterService::OnNotifySyncExperience>(this);

    m_dialogueEventConnection = m_dispatcher.sink<DialogueEvent>().connect<&CharacterService::OnDialogueEvent>(this);
    m_dialogueSyncConnection = m_dispatcher.sink<NotifyDialogue>().connect<&CharacterService::OnNotifyDialogue>(this);

    m_subtitleEventConnection = m_dispatcher.sink<SubtitleEvent>().connect<&CharacterService::OnSubtitleEvent>(this);
    m_subtitleSyncConnection = m_dispatcher.sink<NotifySubtitle>().connect<&CharacterService::OnNotifySubtitle>(this);

    m_actorTeleportConnection = m_dispatcher.sink<NotifyActorTeleport>().connect<&CharacterService::OnNotifyActorTeleport>(this);

    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&CharacterService::OnPartyJoinedEvent>(this);
}

void CharacterService::DeleteRemoteEntityComponents(entt::entity aEntity) const noexcept
{
    m_world.remove<FaceGenComponent, InterpolationComponent, RemoteAnimationComponent, RemoteComponent, CacheComponent, WaitingFor3D, PlayerComponent>(aEntity);
}

void CharacterService::DeclineOwnership(const uint32_t aServerId, const uint32_t aOwnershipEpoch) const noexcept
{
    RequestOwnershipTransfer request{};
    request.ServerId = aServerId;
    request.OwnershipEpoch = aOwnershipEpoch;
    request.Reason = OwnershipReleaseReason::DeclineGrant;
    m_transport.Send(request);
}

void CharacterService::ReconcileActorData(
    const entt::entity aEntity, Actor* apActor, const uint32_t aOwnershipEpoch, const ActorData& acActorData, const bool aApplyInventory, const bool aIsLocalOwner) noexcept
{
    if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(aEntity))
    {
        pWaitingFor3D->SpawnRequest.InitialActorValues = acActorData.InitialActorValues;
        pWaitingFor3D->SpawnRequest.InventoryContent = acActorData.InitialInventory;
        pWaitingFor3D->SpawnRequest.IsDead = acActorData.IsDead;
        pWaitingFor3D->SpawnRequest.IsWeaponDrawn = acActorData.IsWeaponDrawn;
        pWaitingFor3D->SpawnRequest.OwnershipEpoch = aOwnershipEpoch;
    }

    if (!apActor)
        return;

    apActor->SetActorValues(acActorData.InitialActorValues);

    if (aApplyInventory)
    {
        const Inventory currentInventory = apActor->GetActorInventory();
        if (currentInventory.Entries != acActorData.InitialInventory.Entries || currentInventory.CurrentMagicEquipment != acActorData.InitialInventory.CurrentMagicEquipment)
            apActor->SetActorInventory(acActorData.InitialInventory);
    }

    if (apActor->IsDead() != acActorData.IsDead)
    {
        if (acActorData.IsDead)
        {
            spdlog::info("Killing actor {:X} to match server state, local owner: {}", apActor->formID, aIsLocalOwner);
            apActor->Kill();
        }
        // Respawn resets the reference, which re-rolls leveled actors and reloads their 3D. The owner would
        // rediscover it as a new actor, so keep the local death and let the death state sync report it.
        else if (aIsLocalOwner)
            spdlog::warn("Keeping actor {:X} dead although the server has it alive", apActor->formID);
        else
        {
            spdlog::info("Respawning actor {:X} to match server state", apActor->formID);
            apActor->Respawn();
        }
    }

    if (aIsLocalOwner)
    {
        // A remote draw correction may still be queued when an ownership grant arrives.
        m_weaponDrawUpdates.erase(apActor->formID);

        if (apActor->actorState.IsWeaponDrawn() != acActorData.IsWeaponDrawn)
            apActor->SetWeaponDrawnEx(acActorData.IsWeaponDrawn);
    }
    else
        m_weaponDrawUpdates[apActor->formID] = {acActorData.IsWeaponDrawn};
}

bool CharacterService::RequestOwnership(const uint32_t aFormId, const uint32_t aServerId, const entt::entity aEntity) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
        if (!pActor)
        {
            spdlog::warn("Cannot request ownership of actor {:X} because its form {:X} is unavailable", aServerId, aFormId);
            return false;
        }

        ActorExtension* pExtension = pActor->GetExtension();
        if (pExtension->IsRemotePlayer())
        {
            spdlog::warn("Cannot request ownership of remote player actor {:X}", aServerId);
            return false;
        }

        if (pActor->IsPlayerSummon())
        {
            spdlog::warn("Cannot request ownership of remote player summon {:X}", aServerId);
            return false;
        }

        const auto* pRemoteComponent = m_world.try_get<RemoteComponent>(aEntity);
        if (!pRemoteComponent || pRemoteComponent->Id != aServerId || pRemoteComponent->OwnershipEpoch == 0)
            return false;

        RequestOwnershipClaim request;
        request.ServerId = aServerId;
        request.ExpectedOwnershipEpoch = pRemoteComponent->OwnershipEpoch;

        if (!m_transport.Send(request))
            return false;

        return true;
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::RequestOwnership");
        return false;
    }
}

void CharacterService::DeleteTempActor(const uint32_t aFormId) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
        if (pActor && ((pActor->formID & 0xFF000000) == 0xFF000000))
        {
            pActor->Delete();
            spdlog::info("\tDeleted actor {:X}", aFormId);
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::DeleteTempActor");
    }
}

void CharacterService::OnActorAdded(const ActorAddedEvent& acEvent) noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId));

    // The discovery sweep can report a form id that no longer resolves: an actor
    // unloaded between the sweep and this call, or a temporary reference the
    // engine already recycled. The player branch used to dereference the result
    // unconditionally, which is a null write on exactly that race.
    if (!pActor)
        return;

    if (acEvent.FormId == 0x14)
    {
        pActor->GetExtension()->SetPlayer(true);
    }

    entt::entity entity;

    const auto view = m_world.view<RemoteComponent>();
    const auto it = std::find_if(
        std::begin(view), std::end(view),
        [&acEvent, view](entt::entity entity)
        {
            auto& remoteComponent = view.get<RemoteComponent>(entity);
            return remoteComponent.CachedRefId == acEvent.FormId;
        });

    if (it != std::end(view))
    {
        // Same actor the check above already resolved; re-fetching it here only
        // gave the compiler a second chance to lose that proof.
        pActor->GetExtension()->SetRemote(true);

        entity = *it;
    }
    else
        entity = m_world.create();

    m_world.emplace_or_replace<FormIdComponent>(entity, acEvent.FormId);
    m_world.emplace_or_replace<EarlyAnimationBufferComponent>(entity);

    ProcessNewEntity(entity);
}

void CharacterService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (auto* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId)))
            pActor->GetExtension()->Reconciliation = ActorExtension::ReconciliationStage::None;

        m_pendingLeveledConforms.erase(acEvent.FormId);

        auto view = m_world.view<FormIdComponent>();
        const auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.FormId](auto aEntity) { return view.get<FormIdComponent>(aEntity).Id == formId; });

        if (entityIt == view.end())
        {
            spdlog::error("Actor to remove not found in form ids map {:X}", acEvent.FormId);
            return;
        }

        const auto cId = *entityIt;

        auto& formIdComponent = view.get<FormIdComponent>(cId);
        CancelServerAssignment(*entityIt, formIdComponent.Id);

        m_world.remove<EarlyAnimationBufferComponent>(cId);

        if (m_world.all_of<FormIdComponent>(cId))
            m_world.remove<FormIdComponent>(cId);

        if (m_world.orphan(cId))
            m_world.destroy(cId);

        spdlog::info("Actor removed, form id: {:X}", acEvent.FormId);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnActorRemoved");
    }
}

void CharacterService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    UpdateRemoteFaceGenWindow();
    RunSpawnUpdates();
    RunLocalUpdates();
    RunFactionsUpdates();
    RunRemoteUpdates();
    RunExperienceUpdates();
    ApplyCachedWeaponDraws(acUpdateEvent);
    ProcessLeveledConforms();
}

void CharacterService::OnConnected(const ConnectedEvent& acConnectedEvent) const noexcept
{
    // Go through all the forms that were previously detected
    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    Vector<entt::entity> entities(view.begin(), view.end());

    for (auto entity : entities)
    {
        auto& formIdComponent = m_world.get<FormIdComponent>(entity);
        // Delete all temporary actors on connect
        if (formIdComponent.Id > 0xFF000000)
        {
            Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (pActor)
                pActor->Delete();

            continue;
        }

        ProcessNewEntity(entity);
    }
}

void CharacterService::OnDisconnected(const DisconnectedEvent& acDisconnectedEvent) const noexcept
{
    auto remoteView = m_world.view<FormIdComponent, RemoteComponent>();
    for (auto entity : remoteView)
    {
        auto& formIdComponent = remoteView.get<FormIdComponent>(entity);

        auto pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            continue;

        if (pActor->GetExtension()->IsRemotePlayer())
            pActor->Delete();
        else
            pActor->GetExtension()->SetRemote(false);
    }

    m_world.clear<WaitingForAssignmentComponent, LocalComponent, RemoteComponent>();

    for (const auto& [formId, pickFormId] : m_pendingLeveledConforms)
    {
        if (auto* pActor = Cast<Actor>(TESForm::GetById(formId)))
        {
            auto& stage = pActor->GetExtension()->Reconciliation;
            // Don't leave the actor disabled if we disconnect before re-enabling it.
            if (stage == ActorExtension::ReconciliationStage::WaitingForDisable && !pActor->IsDeleted())
                pActor->EnableImpl();

            stage = ActorExtension::ReconciliationStage::None;
        }
    }

    m_pendingLeveledConforms.clear();
}

void CharacterService::OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        spdlog::info(__FUNCTION__ ": Received for cookie {:X}, server id {:X}", acMessage.Cookie, acMessage.ServerId);

        auto view = m_world.view<WaitingForAssignmentComponent>();
        const auto itor = std::find_if(std::begin(view), std::end(view), [view, cookie = acMessage.Cookie](auto entity) { return view.get<WaitingForAssignmentComponent>(entity).Cookie == cookie; });

        if (itor == std::end(view))
        {
            spdlog::warn("Never found requested cookie: {}", acMessage.Cookie);
            return;
        }

        const auto cEntity = *itor;
        const bool isCancelled = view.get<WaitingForAssignmentComponent>(cEntity).Cancelled;

        m_world.remove<WaitingForAssignmentComponent>(cEntity);
    #if (!IS_MASTER)
        m_world.remove<ReplayedActionsDebugComponent>(cEntity);
    #endif

        if (isCancelled)
        {
            if (acMessage.Owner)
                DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);

            if (m_world.valid(cEntity))
                m_world.destroy(cEntity);

            return;
        }

        if (acMessage.OwnershipEpoch == 0)
        {
            spdlog::warn("Ignored assignment for actor {:X} because the server returned an invalid ownership epoch", acMessage.ServerId);
            return;
        }

        const auto formIdComponent = m_world.try_get<FormIdComponent>(cEntity);
        if (!formIdComponent)
        {
            if (acMessage.Owner)
                DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);

            if (m_world.valid(cEntity))
                m_world.destroy(cEntity);

            spdlog::warn("Discarded assignment for actor {:X} because the local entity no longer has a form", acMessage.ServerId);
            return;
        }

        Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent->Id));
        if (!pActor)
        {
            if (acMessage.Owner)
                DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);

            spdlog::warn("Discarded assignment for actor {:X} because form {:X} is unavailable", acMessage.ServerId, formIdComponent->Id);
            m_world.destroy(cEntity);
            return;
        }

        // TODO: how could this possibly trigger?
        // it's kinda interfering with my WaitingFor3D code
        if (acMessage.PlayerId != 0)
            m_world.emplace_or_replace<PlayerComponent>(cEntity, acMessage.PlayerId);

        ActorData actorData{};
        actorData.InitialActorValues = acMessage.AllActorValues;
        actorData.InitialInventory = acMessage.CurrentInventory;
        actorData.IsDead = acMessage.IsDead;
        actorData.IsWeaponDrawn = acMessage.IsWeaponDrawn;

        if (acMessage.Owner)
        {
            spdlog::info(__FUNCTION__ ": received local actor, form id: {:X}, {}", pActor->formID, pActor->baseForm ? pActor->baseForm->GetName() : "<no base form>");

            // We are the authority, and the server's data is only an echo of our own request snapshot, which can already be stale.
            // Applying it would e.g. Respawn() (Resurrect + Reset) an actor that was killed while the request was in flight,
            // which re-rolls leveled temporary actors, removes them, and makes them get rediscovered as new entities endlessly.
            // Any local change since the request (e.g. death) is picked up by the usual LocalComponent diffing instead.
            if (pActor->IsDead() != acMessage.IsDead)
                spdlog::info("Local actor {:X} death state changed while its assignment was in flight (server: {}, local: {}), keeping local state", pActor->formID, acMessage.IsDead, pActor->IsDead());

            m_weaponDrawUpdates.erase(pActor->formID);

            auto& localAnimationComponent = m_world.emplace_or_replace<LocalAnimationComponent>(cEntity);

            if (auto* pEarlyAnimComponent = m_world.try_get<EarlyAnimationBufferComponent>(cEntity))
            {
                for (const auto& action : pEarlyAnimComponent->Actions)
                {
                    localAnimationComponent.Append(action);
                }
            }
            m_world.remove<EarlyAnimationBufferComponent>(cEntity);

            auto& localComponent = m_world.emplace_or_replace<LocalComponent>(cEntity, acMessage.ServerId, acMessage.OwnershipEpoch);
            localComponent.IsDead = acMessage.IsDead;
            localComponent.IsWeaponDrawn = acMessage.IsWeaponDrawn;
            pActor->GetExtension()->SetRemote(false);
        }
        else
        {
            spdlog::info(__FUNCTION__ ": received remote actor, form id: {:X}, isweapondrawn: {}, {}", pActor->formID, acMessage.IsWeaponDrawn, pActor->baseForm ? pActor->baseForm->GetName() : "<no base form>");

            m_world.emplace_or_replace<RemoteComponent>(cEntity, acMessage.ServerId, formIdComponent->Id, acMessage.OwnershipEpoch);

            pActor->GetExtension()->SetRemote(true);

            m_world.remove<EarlyAnimationBufferComponent>(cEntity);
            InterpolationSystem::Setup(m_world, cEntity);
            AnimationSystem::Setup(m_world, cEntity);
            AnimationSystem::AddActionsForReplay(m_world.get<RemoteAnimationComponent>(cEntity), acMessage.ActionsToReplay);

    #if (!IS_MASTER)
            m_world.emplace_or_replace<ReplayedActionsDebugComponent>(cEntity, acMessage.ActionsToReplay);
    #endif

            ReconcileActorData(cEntity, pActor, acMessage.OwnershipEpoch, actorData, true, false);

            MoveActor(pActor, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);

            // The owner's leveled pick rides the assignment response for actors we discovered ourselves
            ApplyLeveledNpcPick(pActor, acMessage.LeveledNpcPickId);
        }
        pActor->GetExtension()->SetNakedDeadline();
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnAssignCharacter");
    }
}

void CharacterService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (acMessage.OwnershipEpoch == 0)
        {
            spdlog::warn("Ignored spawn for actor {:X} because the ownership epoch is invalid", acMessage.ServerId);
            return;
        }

        auto remoteView = m_world.view<RemoteComponent>();
        const auto remoteItor = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ServerId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

        if (remoteItor != std::end(remoteView))
        {
            spdlog::warn("Character with remote id {:X} is already spawned.", acMessage.ServerId);
            return;
        }

        // Same for an actor this client already owns. Re-adding it as a remote spawn
        // puts a second actor in the world for one server id, which is the state the
        // engine crashes on.
        auto localView = m_world.view<LocalComponent>();
        const auto localItor = std::find_if(std::begin(localView), std::end(localView), [localView, Id = acMessage.ServerId](auto entity) { return localView.get<LocalComponent>(entity).Id == Id; });

        if (localItor != std::end(localView))
        {
            spdlog::warn("Character with remote id {:X} is already owned locally.", acMessage.ServerId);
            return;
        }

        Actor* pActor = nullptr;

        std::optional<entt::entity> entity;

        // Custom forms
        if (acMessage.FormId == GameId{})
        {
            TESNPC* pNpc = nullptr;

            entity = m_world.create();

            if (acMessage.BaseId != GameId{})
            {
                // Prefer the owner's resolved leveled pick over the lossy template base
                if (acMessage.LeveledNpcPickId != GameId{})
                    pNpc = Cast<TESNPC>(TESForm::GetById(m_world.GetModSystem().GetGameId(acMessage.LeveledNpcPickId)));

                if (!pNpc)
                    pNpc = Cast<TESNPC>(TESForm::GetById(m_world.GetModSystem().GetGameId(acMessage.BaseId)));

                if (!pNpc)
                {
                    spdlog::error("Failed to retrieve NPC, it will not be spawned, possibly missing mod, base: {:X}:{:X}, form: {:X}:{:X}", acMessage.BaseId.BaseId, acMessage.BaseId.ModId, acMessage.FormId.BaseId, acMessage.FormId.ModId);
                    return;
                }

                pNpc->Deserialize(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            }
            else
            {
                // Players and npcs with temporary ref ids and base ids (usually random events)
                pNpc = TESNPC::Create(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
                FaceGenSystem::Setup(m_world, *entity, acMessage.FaceTints);
            }

            // The engine builds this body on a later tick, and SetRemoteFaceGenForce has
            // to be in effect before it does.
            SetRemoteFaceGenForce(true);

            pActor = Actor::Create(pNpc);
        }
        else
        {
            const uint32_t cActorId = World::Get().GetModSystem().GetGameId(acMessage.FormId);

            auto waitingView = m_world.view<FormIdComponent, WaitingForAssignmentComponent>();
            const auto waitingItor = std::find_if(std::begin(waitingView), std::end(waitingView), [waitingView, cActorId](auto entity) { return waitingView.get<FormIdComponent>(entity).Id == cActorId; });

            if (waitingItor != std::end(waitingView))
            {
                spdlog::info("Character with form id {:X} already has a spawn request in progress.", cActorId);
                return;
            }

            auto* const pForm = TESForm::GetById(cActorId);
            pActor = Cast<Actor>(pForm);

            if (!pActor)
            {
                spdlog::error("Failed to retrieve Actor {:X}, it will not be spawned, possibly missing mod", cActorId);
                spdlog::error("\tForm : {:X}", pForm ? pForm->formID : 0);
                return;
            }

            const auto view = m_world.view<FormIdComponent>();
            const auto itor = std::find_if(std::begin(view), std::end(view), [cActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == cActorId; });

            if (itor != std::end(view))
                entity = *itor;
            else
                entity = m_world.create();
        }

        if (!pActor)
        {
            spdlog::error("Actor object {:X} could not be created.", acMessage.ServerId);
            return;
        }

        spdlog::info("CharacterSpawnRequest, server id: {:X}, form id: {:X}", acMessage.ServerId, pActor->formID);

        // Pending reconciliation re-enables the actor after applying the owner's pick.
        if (pActor->IsDisabled() && pActor->GetExtension()->Reconciliation != ActorExtension::ReconciliationStage::WaitingForDisable)
        {
            spdlog::warn("Disabled actor is being re-enabled: {:X}", pActor->formID);
            pActor->EnableImpl();
        }

        pActor->GetExtension()->SetRemote(true);

        pActor->rotation.x = acMessage.Rotation.x;
        pActor->rotation.z = acMessage.Rotation.y;

        // The player is null on the main menu and while a save is loading, where a
        // spawn request can already arrive; the cell is only the anchor for MoveTo.
        if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
            pActor->MoveTo(pPlayer->GetParentCellEx(), acMessage.Position);

        pActor->SetActorValues(acMessage.InitialActorValues);

        pActor->GetExtension()->SetPlayer(acMessage.IsPlayer);
        if (acMessage.IsPlayer)
        {
            pActor->SetIgnoreFriendlyHit(true);
            pActor->SetPlayerRespawnMode();
            m_world.emplace_or_replace<PlayerComponent>(*entity, acMessage.PlayerId);
        }

        if (pActor->IsDead() != acMessage.IsDead)
            acMessage.IsDead ? pActor->Kill() : pActor->Respawn();

        spdlog::info("Spawn Request Is summon {}", acMessage.IsPlayerSummon);

        if (acMessage.IsPlayerSummon)
        {
            // Prevents remote summons agroing other players.
            if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
                pActor->SetCommandingActor(pPlayer->GetHandle());
        }

        // Static references arrive with their own locally rolled leveled pick; conform to the owner's.
        if (acMessage.FormId != GameId{})
            ApplyLeveledNpcPick(pActor, acMessage.LeveledNpcPickId);

        m_world.emplace_or_replace<RemoteComponent>(*entity, acMessage.ServerId, pActor->formID, acMessage.OwnershipEpoch);

        auto& interpolationComponent = InterpolationSystem::Setup(m_world, *entity);
        interpolationComponent.Position = acMessage.Position;

        AnimationSystem::Setup(m_world, *entity);

        m_world.emplace_or_replace<WaitingFor3D>(*entity, acMessage);

        auto& remoteAnimationComponent = m_world.get<RemoteAnimationComponent>(*entity);

        AnimationSystem::AddActionsForReplay(remoteAnimationComponent, acMessage.ActionsToReplay);

    #if (!IS_MASTER)
        m_world.emplace_or_replace<ReplayedActionsDebugComponent>(*entity, acMessage.ActionsToReplay);
    #endif
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnCharacterSpawn");
    }
}

void CharacterService::OnReferencesMoveRequest(const ServerReferencesMoveRequest& acMessage) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (acMessage.Updates.empty())
            return;

        auto view = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent>();

        // Index the view once instead of scanning it per entry. The scan made this
        // O(updates x entities) on the movement path, and a busy cell puts one entry
        // per actor in range into every packet.
        TiltedPhoques::Map<uint32_t, entt::entity> byServerId;
        for (auto entity : view)
            byServerId[view.get<RemoteComponent>(entity).Id] = entity;

        for (const auto& [serverId, update] : acMessage.Updates)
        {
            const auto itor = byServerId.find(serverId);

            if (itor == std::end(byServerId))
                continue;

            const auto entity = itor->second;
            auto& interpolationComponent = view.get<InterpolationComponent>(entity);
            auto& animationComponent = view.get<RemoteAnimationComponent>(entity);
            const auto& movement = update.UpdatedMovement;

            InterpolationComponent::TimePoint point;
            // Per-actor sample time when the sender supplied one; a server that
            // predates this field sends zero, where the packet's relay time is
            // the closest approximation available.
            point.Tick = update.SampleTick != 0 ? update.SampleTick : acMessage.Tick;
            point.Position = movement.Position;
            point.Rotation = {movement.Rotation.x, 0.f, movement.Rotation.y};
            point.Variables = movement.Variables;
            point.Direction = movement.Direction;

            InterpolationSystem::AddPoint(interpolationComponent, point);

            for (const auto& action : update.ActionEvents)
            {
                animationComponent.TimePoints.push_back(action);
            }
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnReferencesMoveRequest");
    }
}

void CharacterService::OnActionEvent(const ActionEvent& acActionEvent) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        auto view = m_world.view<LocalAnimationComponent, FormIdComponent>();
        const auto itor = std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (itor != std::end(view))
        {
            auto& localComponent = view.get<LocalAnimationComponent>(*itor);

            localComponent.Append(acActionEvent);
        }
        else if (m_transport.IsOnline())
        {
            // A `LocalAnimationComponent` is not attached yet, but the actor already exists and is running animations

            auto view = m_world.view<FormIdComponent, EarlyAnimationBufferComponent>();
            const auto itor = std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

            if (itor != std::end(view))
            {
                view.get<EarlyAnimationBufferComponent>(*itor).Actions.push_back(acActionEvent);
            }
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnActionEvent");
    }
}

void CharacterService::OnFactionsChanges(const NotifyFactionsChanges& acEvent) const noexcept
{
    auto view = m_world.view<RemoteComponent, FormIdComponent, CacheComponent>();

    for (const auto& [id, factions] : acEvent.Changes)
    {
        const auto itor = std::find_if(std::begin(view), std::end(view), [id = id, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

        if (itor != std::end(view))
        {
            auto& formIdComponent = view.get<FormIdComponent>(*itor);

            auto* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (!pActor)
                continue;

            auto& cacheComponent = view.get<CacheComponent>(*itor);
            cacheComponent.FactionsContent = factions;

            pActor->SetFactions(cacheComponent.FactionsContent);
        }
    }
}

void CharacterService::OnOwnershipTransfer(const NotifyOwnershipTransfer& acMessage) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (acMessage.OwnershipEpoch == 0)
        {
            spdlog::warn("Ignored ownership update for actor {:X} because the epoch is invalid", acMessage.ServerId);
            return;
        }

        auto entity = Utils::FindEntityByServerId(acMessage.ServerId);
        if (entity && !m_world.any_of<LocalComponent, RemoteComponent>(*entity))
            entity.reset();

        uint32_t currentEpoch = 0;
        if (entity)
        {
            if (const auto* pLocalComponent = m_world.try_get<LocalComponent>(*entity))
                currentEpoch = pLocalComponent->OwnershipEpoch;
            else if (const auto* pRemoteComponent = m_world.try_get<RemoteComponent>(*entity))
                currentEpoch = pRemoteComponent->OwnershipEpoch;
        }

        if (currentEpoch != 0 && acMessage.OwnershipEpoch <= currentEpoch)
        {
            spdlog::debug("Ignored stale ownership update for actor {:X} at epoch {}; current epoch is {}", acMessage.ServerId, acMessage.OwnershipEpoch, currentEpoch);
            return;
        }

        const bool isLocalOwner = acMessage.OwnerPlayerId == m_transport.GetLocalPlayerId();
        if (!entity)
        {
            // A transfer does not contain enough form data to recreate an unknown actor. Decline so the server can try another loaded client.
            if (isLocalOwner)
                DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);
            else
                spdlog::debug("Ignored ownership update for unknown actor {:X} at epoch {}", acMessage.ServerId, acMessage.OwnershipEpoch);
            return;
        }

        const entt::entity cEntity = *entity;
        const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(cEntity);
        Actor* pActor = pFormIdComponent ? Cast<Actor>(TESForm::GetById(pFormIdComponent->Id)) : nullptr;

        // Preserve the accepted epoch's pick for actors that still need to be created.
        if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(cEntity))
            pWaitingFor3D->SpawnRequest.LeveledNpcPickId = acMessage.LeveledNpcPickId;

        if (isLocalOwner)
        {
            if (!pFormIdComponent || !pActor || !pActor->GetNiNode())
            {
                uint32_t cachedRefId = pFormIdComponent ? pFormIdComponent->Id : 0;
                if (const auto* pRemoteComponent = m_world.try_get<RemoteComponent>(cEntity))
                    cachedRefId = pRemoteComponent->CachedRefId;

                if (pActor)
                    pActor->GetExtension()->SetRemote(true);

                m_world.remove<LocalAnimationComponent, LocalComponent>(cEntity);
                if (m_world.all_of<RemoteComponent>(cEntity))
                    m_world.get<RemoteComponent>(cEntity).OwnershipEpoch = acMessage.OwnershipEpoch;
                else if (cachedRefId != 0)
                    m_world.emplace<RemoteComponent>(cEntity, acMessage.ServerId, cachedRefId, acMessage.OwnershipEpoch);

                spdlog::warn("Declined ownership of actor {:X} at epoch {} because the actor is not ready", acMessage.ServerId, acMessage.OwnershipEpoch);
                DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);
                return;
            }

            // Reconcile while hooks still treat the actor as remote/non-authoritative.
            pActor->GetExtension()->SetRemote(true);
            m_world.remove<LocalAnimationComponent, LocalComponent>(cEntity);
            m_world.emplace_or_replace<RemoteComponent>(cEntity, acMessage.ServerId, pFormIdComponent->Id, acMessage.OwnershipEpoch);

            ReconcileActorData(cEntity, pActor, acMessage.OwnershipEpoch, acMessage.CurrentActorData, true, true);
            ApplyLeveledNpcPick(pActor, acMessage.LeveledNpcPickId);

            DeleteRemoteEntityComponents(cEntity);
            CacheSystem::Setup(m_world, cEntity, pActor);
            m_world.emplace_or_replace<LocalAnimationComponent>(cEntity);
            auto& localComponent = m_world.emplace_or_replace<LocalComponent>(cEntity, acMessage.ServerId, acMessage.OwnershipEpoch);
            localComponent.IsDead = acMessage.CurrentActorData.IsDead;
            localComponent.IsWeaponDrawn = acMessage.CurrentActorData.IsWeaponDrawn;

            // LocalComponent is installed only after canonical reconciliation is complete.
            pActor->GetExtension()->SetRemote(false);
            spdlog::info("Gained ownership of actor {:X} at epoch {}", acMessage.ServerId, acMessage.OwnershipEpoch);
            return;
        }

        if (pActor)
            pActor->GetExtension()->SetRemote(true);

        m_world.remove<LocalAnimationComponent, LocalComponent>(cEntity);

        if (pFormIdComponent)
        {
            m_world.emplace_or_replace<RemoteComponent>(cEntity, acMessage.ServerId, pFormIdComponent->Id, acMessage.OwnershipEpoch);

            if (!m_world.all_of<InterpolationComponent>(cEntity))
                InterpolationSystem::Setup(m_world, cEntity);
            if (!m_world.all_of<RemoteAnimationComponent>(cEntity))
                AnimationSystem::Setup(m_world, cEntity);
        }
        else if (auto* pRemoteComponent = m_world.try_get<RemoteComponent>(cEntity))
        {
            pRemoteComponent->OwnershipEpoch = acMessage.OwnershipEpoch;
        }

        ReconcileActorData(cEntity, pActor, acMessage.OwnershipEpoch, acMessage.CurrentActorData, pActor && pActor->GetNiNode(), false);
        if (pActor)
            ApplyLeveledNpcPick(pActor, acMessage.LeveledNpcPickId);

        spdlog::info("Actor {:X} is now owned by player {:X} at epoch {}", acMessage.ServerId, acMessage.OwnerPlayerId, acMessage.OwnershipEpoch);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnOwnershipTransfer");
    }
}

void CharacterService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acMessage.ServerId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        if (auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*itor))
        {
            Actor* pActor = Cast<Actor>(TESForm::GetById(pFormIdComponent->Id));
            if (pActor && pActor->IsTemporary())
                CharacterService::DeleteTempActor(pFormIdComponent->Id);
            else if (pActor)
                pActor->GetExtension()->SetRemote(false);
        }

        DeleteRemoteEntityComponents(*itor);
    }
}

void CharacterService::OnNotifyRespawn(const NotifyRespawn& acMessage) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        auto view = m_world.view<FormIdComponent, RemoteComponent>();
        const auto entityIt = std::find_if(view.begin(), view.end(), [view, id = acMessage.ActorId](auto aEntity) { return view.get<RemoteComponent>(aEntity).Id == id; });

        if (entityIt == view.end())
        {
            spdlog::error("Actor to respawn not found in: {:X}", acMessage.ActorId);
            return;
        }

        const auto cId = *entityIt;

        auto& formIdComponent = view.get<FormIdComponent>(cId);
        CancelServerAssignment(*entityIt, formIdComponent.Id);

        m_world.remove<EarlyAnimationBufferComponent>(cId);

        if (m_world.all_of<FormIdComponent>(cId))
            m_world.remove<FormIdComponent>(cId);

        if (m_world.orphan(cId))
            m_world.destroy(cId);

        RequestRespawn request;
        request.ActorId = acMessage.ActorId;

        m_transport.Send(request);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnNotifyRespawn");
    }
}

void CharacterService::OnBeastFormChange(const BeastFormChangeEvent& acEvent) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        auto view = m_world.view<FormIdComponent>();

        const auto it = std::find_if(view.begin(), view.end(), [view](auto entity) { return view.get<FormIdComponent>(entity).Id == 0x14; });

        std::optional<uint32_t> serverIdRes = Utils::GetServerId(*it);
        if (!serverIdRes.has_value())
        {
            spdlog::error("{}: failed to find server id", __FUNCTION__);
            return;
        }

        uint32_t serverId = serverIdRes.value();

        RequestRespawn request;
        request.ActorId = serverId;

        Actor* pActor = Utils::GetByServerId<Actor>(serverId);
        if (!pActor)
        {
            spdlog::warn(__FUNCTION__ ": could not find actor for server id {:X}", serverId);
            return;
        }

        TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
        if (!pNpc)
        {
            spdlog::warn(__FUNCTION__ ": could not find actor baseform for server id {:X}", serverId);
            return;
        }

        pNpc->Serialize(&request.AppearanceBuffer);
        request.ChangeFlags = pNpc->GetChangeFlags();

        m_transport.Send(request);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnBeastFormChange");
    }
}

void CharacterService::OnMountEvent(const MountEvent& acEvent) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        auto view = m_world.view<FormIdComponent>();

        const auto riderIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.RiderID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (riderIt == std::end(view))
        {
            spdlog::warn("Rider not found, form id: {:X}", acEvent.RiderID);
            return;
        }

        const entt::entity cRiderEntity = *riderIt;
        const auto* pRiderLocalComponent = m_world.try_get<LocalComponent>(cRiderEntity);
        if (!pRiderLocalComponent)
            return;

        const auto mountIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.MountID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (mountIt == std::end(view))
        {
            spdlog::warn("Mount not found, form id: {:X}", acEvent.MountID);
            return;
        }

        const entt::entity cMountEntity = *mountIt;

        uint32_t mountServerId = 0;
        uint32_t mountOwnershipEpoch = 0;
        if (const auto* pMountLocalComponent = m_world.try_get<LocalComponent>(cMountEntity))
        {
            mountServerId = pMountLocalComponent->Id;
            mountOwnershipEpoch = pMountLocalComponent->OwnershipEpoch;
        }
        else if (const auto* pMountRemoteComponent = m_world.try_get<RemoteComponent>(cMountEntity))
        {
            mountServerId = pMountRemoteComponent->Id;
            mountOwnershipEpoch = pMountRemoteComponent->OwnershipEpoch;
        }
        else
            return;

        MountRequest request{};
        request.RiderId = pRiderLocalComponent->Id;
        request.RiderOwnershipEpoch = pRiderLocalComponent->OwnershipEpoch;
        request.MountId = mountServerId;
        request.MountOwnershipEpoch = mountOwnershipEpoch;

        m_transport.Send(request);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnMountEvent");
    }
}

void CharacterService::OnNotifyMount(const NotifyMount& acMessage) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();

        const auto riderIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.RiderId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

        if (riderIt == std::end(remoteView))
        {
            spdlog::warn("Rider with remote id {:X} not found.", acMessage.RiderId);
            return;
        }

        auto& riderFormIdComponent = remoteView.get<FormIdComponent>(*riderIt);
        TESForm* pRiderForm = TESForm::GetById(riderFormIdComponent.Id);
        Actor* pRider = Cast<Actor>(pRiderForm);
        if (!pRider)
            return;

        const auto mountIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.MountId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });
        if (mountIt == std::end(remoteView))
        {
            spdlog::warn("Cannot apply mount update because mount {:X} is unavailable", acMessage.MountId);
            return;
        }

        const auto& mountFormIdComponent = remoteView.get<FormIdComponent>(*mountIt);
        Actor* pMount = Cast<Actor>(TESForm::GetById(mountFormIdComponent.Id));
        if (!pMount)
            return;

        pRider->InitiateMountPackage(pMount);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnNotifyMount");
    }
}

void CharacterService::OnInitPackageEvent(const InitPackageEvent& acEvent) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (!m_transport.IsConnected())
            return;

        auto view = m_world.view<FormIdComponent>();

        const auto actorIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.ActorId, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (actorIt == std::end(view))
            return;

        const entt::entity cActorEntity = *actorIt;

        std::optional<uint32_t> actorServerIdRes = Utils::GetServerId(cActorEntity);
        if (!actorServerIdRes.has_value())
        {
            spdlog::error("{}: failed to find server id", __FUNCTION__);
            return;
        }

        NewPackageRequest request;
        request.ActorId = actorServerIdRes.value();
        if (!m_world.GetModSystem().GetServerModId(acEvent.PackageId, request.PackageId.ModId, request.PackageId.BaseId))
            return;

        m_transport.Send(request);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnInitPackageEvent");
    }
}

void CharacterService::OnNotifyNewPackage(const NotifyNewPackage& acMessage) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
        const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ActorId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

        if (remoteIt == std::end(remoteView))
        {
            spdlog::warn("Actor for package with remote id {:X} not found.", acMessage.ActorId);
            return;
        }

        auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

        const TESForm* pForm = TESForm::GetById(formIdComponent.Id);
        Actor* pActor = Cast<Actor>(pForm);

        const uint32_t cPackageFormId = World::Get().GetModSystem().GetGameId(acMessage.PackageId);
        const TESForm* pPackageForm = TESForm::GetById(cPackageFormId);
        if (!pPackageForm)
        {
            spdlog::warn("Actor package not found, base id: {:X}, mod id: {:X}", acMessage.PackageId.BaseId, acMessage.PackageId.ModId);
            return;
        }

        TESPackage* pPackage = Cast<TESPackage>(pPackageForm);

        pActor->SetPackage(pPackage);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnNotifyNewPackage");
    }
}

void CharacterService::OnAddExperienceEvent(const AddExperienceEvent& acEvent) noexcept
{
    // Bounded on the way in, because this is where a local measurement becomes a
    // number another player's game will apply. The event carries
    // `newExperience - oldExperience` around the engine's own AddSkillExperience,
    // which a mod can drive past what a float holds and a load/save transition
    // between the two reads can turn meaningless. The wire clamps the magnitude on
    // read as well; bounding here keeps the accumulator from carrying a value that
    // only the receiver would reject.
    m_cachedExperience += WireLimits::ClampExperienceDelta(acEvent.Experience);
}

void CharacterService::OnNotifySyncExperience(const NotifySyncExperience& acMessage) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    if (PlayerCharacter::LastUsedCombatSkill == -1)
        return;

    // The wire already bounded this on read; bounding again at the call that hands
    // it to the engine is deliberate. This is the last point before the value
    // becomes a skill's stored xp, and the engine compares that against a per-level
    // threshold - so a value it cannot compare against leaves the skill in a state
    // no later award can fix.
    pPlayer->AddSkillExperience(PlayerCharacter::LastUsedCombatSkill,
                                WireLimits::ClampExperienceDelta(acMessage.Experience));
}

void CharacterService::OnDialogueEvent(const DialogueEvent& acEvent) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (!m_transport.IsConnected())
            return;

        const bool isLeader = World::Get().GetPartyService().IsLeader(); // Helps distinguish in 2-party logs
        auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
        auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.ActorID](auto entity) {
            return view.get<FormIdComponent>(entity).Id == formId;
        });

        if (entityIt == view.end())
        {
            spdlog::debug(__FUNCTION__ ": failed to find speaking Actor's FormIdComponent, formId {:X}, isLeader {}", acEvent.ActorID, isLeader);
            return;
        }

        auto serverIdRes = Utils::GetServerId(*entityIt);
        if (!serverIdRes)
        {
            spdlog::debug(__FUNCTION__ ": server id not found for formId {:X}, isLeader {}", acEvent.ActorID, isLeader);
            return;
        }
    
        Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.ActorID));
        if (!pActor)
            return;

        bool isLocal = pActor->GetExtension()->IsLocal();
        bool isInScene = pActor->IsInScene();
        auto sceneId = isInScene ? pActor->GetCurrentScene()->formID : 0;
        bool isTaskDialogue = pActor->IsTalking() && pActor->IsInDialogueWithPlayer();
        bool isSpeakingInScene = pActor->IsSpeakingInScene();

        const bool willSync = isTaskDialogue || isLocal && !isInScene;

        spdlog::debug(
            __FUNCTION__ ": isLocal {}, isInScene {}, isSpeakingInScene {}, isTaskDialogue {}, willSync {}, scene {:X}, Actor "
                         "{:X}, serverId {:X}, isLeader {}, name {}, soundFile {}",
            isLocal, isInScene, isSpeakingInScene, isTaskDialogue, willSync, sceneId, pActor->formID, serverIdRes.value(), isLeader,
            pActor->baseForm ? pActor->baseForm->GetName() : "<no base form>", acEvent.VoiceFile);

        if (willSync)
        {
            DialogueRequest request{};
            request.ServerId = serverIdRes.value();
            request.SoundFilename = acEvent.VoiceFile;

            m_transport.Send(request);
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnDialogueEvent");
    }
}

void CharacterService::OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        // A member can initiate dialogue with an NPC owned by this client.
        const bool isLeader = World::Get().GetPartyService().IsLeader(); // Helps distinguish in 2-party logs
        Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
        if (!pActor)
            return;

        spdlog::debug(
            __FUNCTION__ ": playing dialogue Actor {:X}, serverId {:X}, isLeader {}, name {}, soundFile {}", pActor->formID, acMessage.ServerId, isLeader, pActor->baseForm->GetName(),
            acMessage.SoundFilename);

        pActor->StopCurrentDialogue(true);
        pActor->SpeakSound(acMessage.SoundFilename.c_str());
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnNotifyDialogue");
    }
}

void CharacterService::OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (!m_transport.IsConnected())
            return;

        const bool isLeader = World::Get().GetPartyService().IsLeader(); // Helps distinguish in 2-party logs
        auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
        auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.SpeakerID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

        if (entityIt == view.end())
        {
            spdlog::debug(__FUNCTION__ ": failed to find subtitle Actor's FormIdComponent, formId {:X}, isLeader {}", acEvent.SpeakerID, isLeader);
            return;
        }

        auto serverIdRes = Utils::GetServerId(*entityIt);
        if (!serverIdRes)
        {
            spdlog::debug(__FUNCTION__ ": server id not found for formId {:X}, isLeader {}", acEvent.SpeakerID, isLeader);
            return;
        }

        Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.SpeakerID));
        if (!pActor)
            return;

        bool isLocal = pActor->GetExtension()->IsLocal();
        bool isInScene = pActor->IsInScene();
        auto isSpeakingInScene = pActor->IsSpeakingInScene();
        auto sceneId = isInScene ? pActor->GetCurrentScene()->formID : 0;
        bool isTaskDialogue = pActor->IsTalking() && pActor->IsInDialogueWithPlayer();

        const bool willSync = isTaskDialogue || isLocal && !isInScene;

        spdlog::debug(
            __FUNCTION__ ": isLocal {}, isInScene {}, isSpeakingInScene {}, isTaskDialogue {}, willSync {}, scene {:X}, Actor "
                         "{:X}, serverId {:X}, isLeader {}, name {}, subtitle {}",
            isLocal, isInScene, isSpeakingInScene, isTaskDialogue, willSync, sceneId, pActor->formID, serverIdRes.value(), isLeader,
            pActor->baseForm ? pActor->baseForm->GetName() : "<no base form>", acEvent.Text);

        if (willSync)
        {
            SubtitleRequest request{};
            request.ServerId = serverIdRes.value();
            request.Text = acEvent.Text;
            request.TopicFormId = acEvent.TopicFormID;
            m_transport.Send(request);
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnSubtitleEvent");
    }
}

void CharacterService::OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        const bool isLeader = World::Get().GetPartyService().IsLeader(); // Helps distinguish in 2-party logs
        Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
        if (!pActor)
            return;

        // This is only for fallout 4
        TESTopicInfo* pInfo = nullptr;
        pInfo = Cast<TESTopicInfo>(TESForm::GetById(acMessage.TopicFormId));

        spdlog::debug(__FUNCTION__ ": showing subtitle Actor {:X}, serverId {:X}, isLeader {}, name {}, message: {}",
                      pActor->formID, acMessage.ServerId, isLeader, pActor->baseForm ? pActor->baseForm->GetName() : "<no base form>",
                      acMessage.Text);

        // The subtitle singleton is only filled once the engine's dialogue
        // system is up, and this handler runs off a wire packet, so it can
        // arrive before that. Both calls are best-effort cosmetics: a null
        // manager means no subtitles yet, not a fault.
        if (auto* pSubtitles = SubtitleManager::Get())
        {
            pSubtitles->HideSubtitle(pActor);   // Subtitle conflicts can hang, this makes it beter at least.
            pSubtitles->ShowSubtitle(pActor, acMessage.Text.c_str(), pInfo);
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnNotifySubtitle");
    }
}

void CharacterService::OnNotifyActorTeleport(const NotifyActorTeleport& acMessage) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        auto& modSystem = m_world.GetModSystem();

        const uint32_t cActorId = World::Get().GetModSystem().GetGameId(acMessage.FormId);
        Actor* pActor = Cast<Actor>(TESForm::GetById(cActorId));
        if (!pActor)
        {
            spdlog::error(__FUNCTION__ ": failed to retrieve actor to teleport.");
            return;
        }

        MoveActor(pActor, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);

        spdlog::info("Successfully teleported actor, form id: {:X}, world space: {:X}, cell: {:X}, position: ({}, {}, {})", pActor->formID, acMessage.WorldSpaceId.BaseId, acMessage.CellId.BaseId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::OnNotifyActorTeleport");
    }
}

void CharacterService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    // Takes ownership of all actors
    if (acEvent.IsLeader)
    {
        auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
        Vector<entt::entity> entities(view.begin(), view.end());

        for (auto entity : entities)
            ProcessNewEntity(entity);
    }
}

void CharacterService::MoveActor(const Actor* apActor, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        TESObjectCELL* pCell = nullptr;
        if (!acWorldSpaceId)
        {
            const uint32_t cCellId = m_world.GetModSystem().GetGameId(acCellId);
            pCell = Cast<TESObjectCELL>(TESForm::GetById(cCellId));
        }
        // In case of lazy-loading of exterior cells
        else
        {
            const uint32_t cWorldSpaceId = m_world.GetModSystem().GetGameId(acWorldSpaceId);
            TESWorldSpace* const pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(cWorldSpaceId));
            if (pWorldSpace)
            {
                GridCellCoords coordinates = GridCellCoords::CalculateGridCellCoords(acPosition);
                pCell = pWorldSpace->LoadCell(coordinates.X, coordinates.Y);
            }
        }

        if (!pCell)
        {
            spdlog::error(__FUNCTION__ ": failed to fetch cell to teleport, actor: {:X}, worldspace: {:X}, cell: {:X}, position: {}, {}, {}", apActor->formID, acWorldSpaceId.BaseId, acCellId.BaseId, acPosition.x, acPosition.y, acPosition.z);
            return;
        }

        apActor->MoveTo(pCell, acPosition);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::MoveActor");
    }
}

void CharacterService::ProcessNewEntity(entt::entity aEntity) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (!m_transport.IsOnline())
            return;

        auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

        Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
        {
            spdlog::warn(__FUNCTION__ ": actor for new entity not found, form id: {:X}", formIdComponent.Id);
            return;
        }

        if (auto* pRemoteComponent = m_world.try_get<RemoteComponent>(aEntity); pRemoteComponent)
        {
            // TODO(cosideci): don't just take all actors (i.e. from other parties),
            // maybe check it server side, add a variable to the request.
            if (m_world.GetPartyService().IsLeader() && !pActor->IsTemporary() && !pActor->IsMount())
            {
                spdlog::info("Sending ownership claim for actor {:X} with server id {:X}", pActor->formID, pRemoteComponent->Id);

                RequestOwnership(pActor->formID, pRemoteComponent->Id, aEntity);
            }
            else
                spdlog::debug("New entity remotely managed, form id: {:X}, server id: {:X}", pActor->formID, pRemoteComponent->Id);

            return;
        }

        if (m_world.any_of<RemoteComponent, LocalComponent, WaitingForAssignmentComponent>(aEntity))
            return;

        CacheSystem::Setup(World::Get(), aEntity, pActor);

        RequestServerAssignment(aEntity);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::ProcessNewEntity");
    }
}

void CharacterService::RequestServerAssignment(const entt::entity aEntity) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (!m_transport.IsOnline())
            return;

        static uint32_t sCookieSeed = 0;

        const auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

        auto* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            return;

        TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
        if (!pNpc)
            return;

        AssignCharacterRequest message{};

        message.Cookie = sCookieSeed;

        if (!m_world.GetModSystem().GetServerModId(formIdComponent.Id, message.ReferenceId))
        {
            spdlog::error("Server reference id not found for form id {:X}", formIdComponent.Id);
            return;
        }

        const auto* pParentCell = pActor->GetParentCellEx();
        const uint32_t cParentCellFormId = pParentCell ? pParentCell->formID : 0;

        if (!m_world.GetModSystem().GetServerModId(cParentCellFormId, message.CellId))
        {
            spdlog::error("Server cell id not found for cell id {:X}", cParentCellFormId);
            return;
        }

        if (const auto pWorldSpace = pActor->GetWorldSpace())
        {
            if (!m_world.GetModSystem().GetServerModId(pWorldSpace->formID, message.WorldSpaceId))
                return;
        }

        message.Position = pActor->position;
        message.Rotation.x = pActor->rotation.x;
        message.Rotation.y = pActor->rotation.z;

        // Serialize the base form
        const auto isPlayer = (formIdComponent.Id == 0x14);
        const auto isTemporary = pActor->formID >= 0xFF000000;

        if (isPlayer)
        {
            pNpc->MarkChanged(0x2000800);
        }

        const auto changeFlags = pNpc->GetChangeFlags();

        if (isPlayer || changeFlags != 0)
        {
            message.ChangeFlags = changeFlags;
            pNpc->Serialize(&message.AppearanceBuffer);
        }

        if (isPlayer)
        {
            auto& entries = message.FaceTints.Entries;

            // The player is null on the main menu and while a save is loading, where
            // this assignment is skipped instead of faulting.
            const PlayerCharacter* pPlayer = PlayerCharacter::Get();
            if (!pPlayer)
                return;

            const auto& tints = pPlayer->GetTints();

            entries.resize(tints.length);

            for (auto i = 0u; i < tints.length; ++i)
            {
                entries[i].Alpha = tints[i]->alpha;
                entries[i].Color = tints[i]->color;
                entries[i].Type = tints[i]->type;

                if (tints[i]->texture)
                    entries[i].Name = tints[i]->texture->name.AsAscii();
            }
        }

        if (isPlayer)
        {
            auto& questLog = message.QuestContent.Entries;
            auto& modSystem = m_world.GetModSystem();

            // The player is null on the main menu and while a save is loading, where
            // there is no quest log to send either.
            const PlayerCharacter* pPlayer = PlayerCharacter::Get();
            if (!pPlayer)
                return;

            for (const auto& objective : pPlayer->objectives)
            {
                auto* pQuest = objective.instance->quest;
                if (!pQuest)
                    continue;

                if (!QuestService::IsNonSyncableQuest(pQuest))
                {
                    GameId id{};

                    if (modSystem.GetServerModId(pQuest->formID, id))
                    {
                        auto& entry = questLog.emplace_back();
                        entry.Stage = pQuest->currentStage;
                        entry.Id = id;
                    }
                }
            }

            // remove duplicates
            const auto ip = std::unique(questLog.begin(), questLog.end());
            questLog.resize(std::distance(questLog.begin(), ip));
        }

        message.CurrentActorData = BuildActorData(pActor);

        message.FactionsContent = pActor->GetFactions();
        message.IsDragon = pActor->IsDragon();
        message.IsMount = pActor->IsMount();
        message.IsPlayerSummon = pActor->GetCommandingActor() && pActor->GetCommandingActor()->formID == 0x14;

        if (const TESNPC* pPick = pActor->GetLeveledPick())
        {
            const uint32_t pickFormId = pPick->formID;
            if (m_world.GetModSystem().GetServerModId(pickFormId, message.LeveledNpcPickId))
                spdlog::info("Captured leveled NPC pick {:X} for actor {:X} (base {:X})", pickFormId, pActor->formID, pNpc->formID);
            else
                spdlog::warn("Leveled NPC pick {:X} has no server id, identity sync skipped", pickFormId);
        }
        else if (pNpc->IsTemporary())
        {
            spdlog::info("No leveled pick recoverable for temp base {:X} (actor {:X}), identity sync unavailable", pNpc->formID, pActor->formID);
        }

        if (pNpc->IsTemporary())
            pNpc = pNpc->GetTemplateBase();

        if (isTemporary)
        {
            if (pNpc && !m_world.GetModSystem().GetServerModId(pNpc->formID, message.FormId))
            {
                spdlog::error("Server NPC form id not found for form id {:X}", pNpc->formID);
                return;
            }
        }

        // Serialize actions
        auto* const pExtension = pActor->GetExtension();

        message.LatestAction = pExtension->LatestAnimation;
        pActor->SaveAnimationVariables(message.LatestAction.Variables);

        const bool isLeader = m_world.Get().GetPartyService().IsLeader(); // Distinguish logs in 2-party      
        spdlog::info(__FUNCTION__ ": request id: {:X}, cookie: {:X}, entity: {:X}, isLeader: {}", formIdComponent.Id, sCookieSeed, to_integral(aEntity), isLeader);

        if (m_transport.Send(message))
        {
            m_world.emplace<WaitingForAssignmentComponent>(aEntity, sCookieSeed);

            sCookieSeed++;
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::RequestServerAssignment");
    }
}

void CharacterService::CancelServerAssignment(const entt::entity aEntity, const uint32_t aFormId) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
        const char* pName = (pActor && pActor->baseForm) ? pActor->baseForm->GetName() : "";

        if (m_world.all_of<RemoteComponent>(aEntity))
        {
            if (pActor)
            {
                if (pActor->IsTemporary())
                {
                    spdlog::info("Temporary Remote Deleted {:X}", aFormId);
                    pActor->Delete();
                }
                else
                {
                    pActor->GetExtension()->SetRemote(false);
                }
            }

            DeleteRemoteEntityComponents(aEntity);
        }

        // Keep the cookie until the server response arrives so awarded ownership can be relinquished.
        if (m_world.all_of<WaitingForAssignmentComponent>(aEntity))
        {
            auto& waitingComponent = m_world.get<WaitingForAssignmentComponent>(aEntity);
            waitingComponent.Cancelled = true;
            return;
        }

        if (m_world.all_of<LocalComponent>(aEntity))
        {
            auto& localComponent = m_world.get<LocalComponent>(aEntity);

            RequestOwnershipTransfer request{};
            request.ServerId = localComponent.Id;
            request.OwnershipEpoch = localComponent.OwnershipEpoch;
            request.Reason = OwnershipReleaseReason::Relinquish;

            if (pActor)
            {
                if (!pActor->IsTemporary())
                {
                    auto& modSystem = m_world.GetModSystem();

                    if (TESWorldSpace* pWorldSpace = pActor->GetWorldSpace())
                    {
                        if (!modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId))
                            spdlog::error("World space id not found, despite having a world space, {:X}", pWorldSpace->formID);
                    }

                    if (TESObjectCELL* pCell = pActor->GetParentCellEx())
                    {
                        if (!modSystem.GetServerModId(pCell->formID, request.CellId))
                            spdlog::error("Cell id not found, despite having a cell, {:X}", pCell->formID);
                    }

                    request.Position = pActor->position;
                }
            }


            spdlog::info(
                "Transferring ownership of local actor, server id: {:X}, epoch: {}, worldspace: {:X}, cell: {:X}, position: "
                "({}, {}, {})",
                request.ServerId, request.OwnershipEpoch, request.WorldSpaceId.BaseId, request.CellId.BaseId, request.Position.x, request.Position.y, request.Position.z);

            m_transport.Send(request);

            m_world.remove<LocalAnimationComponent, LocalComponent>(aEntity);
        }

        if (pActor)
            pActor->GetExtension()->SetNakedDeadline();
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::CancelServerAssignment");
    }
}

Actor* CharacterService::CreateCharacterForEntity(entt::entity aEntity) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(aEntity);
        auto* pInterpolationComponent = m_world.try_get<InterpolationComponent>(aEntity);

        if (!pWaitingFor3D || !pInterpolationComponent)
        {
            spdlog::error(__FUNCTION__ ": could not find WaitingFor3D or InterpolationComponent");
            return nullptr;
        }

        auto& acMessage = pWaitingFor3D->SpawnRequest;

        Actor* pActor = nullptr;

        // Custom forms
        if (acMessage.FormId == GameId{})
        {
            TESNPC* pNpc = nullptr;

            if (acMessage.BaseId != GameId{})
            {
                // Prefer the owner's resolved leveled pick over the lossy template base
                if (acMessage.LeveledNpcPickId != GameId{})
                    pNpc = Cast<TESNPC>(TESForm::GetById(m_world.GetModSystem().GetGameId(acMessage.LeveledNpcPickId)));

                if (!pNpc)
                    pNpc = Cast<TESNPC>(TESForm::GetById(m_world.GetModSystem().GetGameId(acMessage.BaseId)));

                if (!pNpc)
                {
                    spdlog::error("Failed to retrieve NPC, it will not be spawned, possibly missing mod");
                    return nullptr;
                }

                pNpc->Deserialize(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            }
            else
            {
                pNpc = TESNPC::Create(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
                FaceGenSystem::Setup(m_world, aEntity, acMessage.FaceTints);
            }

            // As in OnCharacterSpawn: this base came off the wire, so the head has to be
            // generated at runtime once the engine builds the body.
            SetRemoteFaceGenForce(true);

            pActor = Actor::Create(pNpc);
        }

        auto& remoteComponent = m_world.get<RemoteComponent>(aEntity);

        if (!pActor)
        {
            spdlog::error(__FUNCTION__ ": could not spawn actor for remote server id {:X}.", remoteComponent.Id);
            return nullptr;
        }

        pActor->GetExtension()->SetRemote(true);
        pActor->rotation.x = acMessage.Rotation.x;
        pActor->rotation.z = acMessage.Rotation.y;
        if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
            pActor->MoveTo(pPlayer->GetParentCellEx(), pInterpolationComponent->Position);
        pActor->SetActorValues(acMessage.InitialActorValues);

        pActor->GetExtension()->SetPlayer(acMessage.IsPlayer);
        if (acMessage.IsPlayer)
        {
            pActor->SetIgnoreFriendlyHit(true);
            pActor->SetPlayerRespawnMode();
            m_world.emplace_or_replace<PlayerComponent>(aEntity, acMessage.PlayerId);
        }

        if (pActor->IsDead() != acMessage.IsDead)
            acMessage.IsDead ? pActor->Kill() : pActor->Respawn();

        spdlog::info("Spawned character for entity, server id: {:X}", remoteComponent.Id);

        return pActor;
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::CreateCharacterForEntity");
        return nullptr;
    }
}

ActorData CharacterService::BuildActorData(Actor* apActor) const noexcept
{
    ActorData actorData{};
    actorData.InitialActorValues = apActor->GetEssentialActorValues();
    actorData.InitialInventory = apActor->GetActorInventory();
    actorData.IsDead = apActor->IsDead();
    actorData.IsWeaponDrawn = apActor->actorState.IsWeaponFullyDrawn();

    return actorData;
}

void CharacterService::ApplyLeveledNpcPick(Actor* apActor, const GameId& acPickId) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        if (acPickId == GameId{})
            return;

        TESNPC* pBase = Cast<TESNPC>(apActor->baseForm);
        if (!pBase)
            return;

        // The rebuild needs the placed NPC this actor came from; without one there
        // is nothing to copy the pick onto, and overwriting the base would throw
        // away data the pick does not carry (a hold guard's name and outfit).
        if (!LeveledNpcSystem::GetOriginalBase(apActor))
        {
            spdlog::warn("Leveled pick {:x}:{:x} received for actor {:X} without an original leveled base, keeping local base", acPickId.ModId, acPickId.BaseId, apActor->formID);
            return;
        }

        const uint32_t cPickId = World::Get().GetModSystem().GetGameId(acPickId);
        if (cPickId == 0)
        {
            spdlog::warn("Leveled NPC pick {:X}:{:X} not resolvable, possibly missing mod, keeping local pick", acPickId.ModId, acPickId.BaseId);
            return;
        }

        TESNPC* pPick = Cast<TESNPC>(TESForm::GetById(cPickId));
        if (!pPick)
        {
            spdlog::warn("Leveled NPC pick {:X} is not an NPC, keeping local pick", cPickId);
            return;
        }

        const TESNPC* pLocalPick = apActor->GetLeveledPick();
        const uint32_t localPickId = pLocalPick ? pLocalPick->formID : 0;

        // Even a pick matching the current base must supersede pending work.
        if (pBase->IsTemporary() && localPickId == cPickId && m_pendingLeveledConforms.find(apActor->formID) == m_pendingLeveledConforms.end())
        {
            spdlog::info("Leveled actor {:X} already matches owner's pick {:X}", apActor->formID, cPickId);
            return;
        }

        spdlog::info("Queued leveled NPC reconciliation for actor {:X}, base: {:X}, local pick: {:X}, owner's pick: {:X}",
            apActor->formID, pBase->formID, localPickId, cPickId);

        // Defer reference changes to the service update: cell attach may still own the actor here.
        // Queueing to the runner from a drained task would re-lock its drain mutex.
        // Preserve the stage when a newer pick arrives during a disable or rebuild.
        m_pendingLeveledConforms[apActor->formID] = cPickId;
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::ApplyLeveledNpcPick");
    }
}

void CharacterService::ProcessLeveledConforms() noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        using ReconciliationStage = ActorExtension::ReconciliationStage;

        if (m_pendingLeveledConforms.empty())
            return;

        // Never touch references while the loading screen is up - the cell attach
        // owns them and mutating mid-stream crashes the loader
        UI* pUI = UI::Get();
        if (pUI && pUI->GetMenuOpen(BSFixedString("Loading Menu")))
            return;

        for (auto it = m_pendingLeveledConforms.begin(); it != m_pendingLeveledConforms.end();)
        {
            const uint32_t cPickFormId = it->second;

            Actor* pActor = Cast<Actor>(TESForm::GetById(it->first));
            TESNPC* pPick = Cast<TESNPC>(TESForm::GetById(cPickFormId));
            if (!pActor || pActor->IsDeleted() || !pPick)
            {
                if (pActor)
                    pActor->GetExtension()->Reconciliation = ReconciliationStage::None;

                it = m_pendingLeveledConforms.erase(it);
                continue;
            }

            auto& stage = pActor->GetExtension()->Reconciliation;
            if (stage == ReconciliationStage::WaitingFor3D)
            {
                const auto* pCell = pActor->GetParentCellEx();
                if (!pCell || !pCell->IsAttached())
                {
                    spdlog::info("Abandoning leveled NPC reconciliation for actor {:X} because its cell is not attached, pick: {:X}, cell state: {}, disabled: {}",
                        it->first, cPickFormId, pCell ? static_cast<int>(pCell->cellState) : -1, pActor->IsDisabled());
                    stage = ReconciliationStage::None;
                    it = m_pendingLeveledConforms.erase(it);
                    continue;
                }

                if (pActor->IsDisabled() || !pActor->GetNiNode())
                {
                    ++it;
                    continue;
                }

                // Completion is read from the extra data the rebuild wrote, which
                // only exists where the pick write is mapped. Without it the
                // fallback path below sets the base to the pick itself, and the
                // base is then the thing to compare - comparing the recorded pick
                // there would never match and re-disable the actor every update.
                const bool cMatchesPick = LeveledNpcSystem::CanRecordPick()
                    ? (pActor->baseForm && pActor->baseForm->IsTemporary() && pActor->GetLeveledPick() == pPick)
                    : (pActor->baseForm == pPick);

                if (cMatchesPick)
                {
                    spdlog::info("Completed leveled NPC reconciliation for actor {:X}, base: {:X}, pick: {:X}", it->first, pActor->baseForm->formID, cPickFormId);
                    stage = ReconciliationStage::None;
                    it = m_pendingLeveledConforms.erase(it);
                    continue;
                }

                // A newer pick arrived during the rebuild; start its disable now.
                stage = ReconciliationStage::None;
            }

            if (stage == ReconciliationStage::WaitingForDisable)
            {
                if (!pActor->IsDisabled() || pActor->GetNiNode())
                {
                    spdlog::debug("Waiting for leveled actor {:X} to finish disabling before applying pick {:X}, disabled: {}, has 3D: {}",
                        it->first, cPickFormId, pActor->IsDisabled(), pActor->GetNiNode() != nullptr);
                    ++it;
                    continue;
                }

                // Disable and 3D teardown have completed; rebuild from the original base and the pick.
                if (LeveledNpcSystem::CanRecordPick())
                {
                    if (!LeveledNpcSystem::ApplyPick(pActor, pPick))
                    {
                        spdlog::warn("Could not rebuild leveled actor {:X} from its original base and pick {:X}, keeping local base", it->first, cPickFormId);
                        pActor->EnableImpl();
                        stage = ReconciliationStage::None;
                        it = m_pendingLeveledConforms.erase(it);
                        continue;
                    }
                }
                else
                {
                    // 1.5.x: the engine call that records the chosen pick has no
                    // address-library mapping, so a rebuild through it could not be
                    // recognised as finished. Keep the pre-1.8.2 path there - the
                    // base becomes the pick, which is what the completion check
                    // above is paired with.
                    pActor->baseForm = pPick;
                }

                // Recompute the graph descriptor after changing picks; stale variable indices can cause out-of-bounds writes.
                pActor->GetExtension()->GraphDescriptorHash = 0;

                // Enable can return before the rebuilt 3D is available to discovery.
                stage = ReconciliationStage::WaitingFor3D;
                pActor->EnableImpl();
                spdlog::info("Re-enabled conformed leveled actor {:X}, base: {:X}, pick: {:X}, waiting for 3D",
                    it->first, pActor->baseForm ? pActor->baseForm->formID : 0, cPickFormId);
                ++it;
                continue;
            }

            if (!pActor->loadedState && !LeveledNpcSystem::IsLeveledNpcBase(Cast<TESNPC>(pActor->baseForm)))
            {
                // Wait for distant actors to load 3D; newer picks replace pending work and disconnects clear it.
                // Unresolved shells bypass this wait because they need a pick before they can load a model.
                ++it;
                continue;
            }

            // DisableImpl() is asynchronous: it only queues a request to disable this actor.
            // Wait for the disabled flag and old 3D removal before changing the base.
            pActor->DisableImpl();
            stage = ReconciliationStage::WaitingForDisable;
            ++it;
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::ProcessLeveledConforms");
    }
}

void CharacterService::RunLocalUpdates() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 100ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    ClientReferencesMoveRequest message;
    message.Tick = m_transport.GetClock().GetCurrentTick();

    auto animatedLocalView = m_world.view<LocalComponent, LocalAnimationComponent, FormIdComponent>();

    for (auto entity : animatedLocalView)
    {
        auto& localComponent = animatedLocalView.get<LocalComponent>(entity);
        auto& animationComponent = animatedLocalView.get<LocalAnimationComponent>(entity);
        auto& formIdComponent = animatedLocalView.get<FormIdComponent>(entity);

        AnimationSystem::Serialize(m_world, message, localComponent, animationComponent, formIdComponent);
    }

    m_transport.Send(message);
}

void CharacterService::SetRemoteFaceGenForce(const bool aForce) const noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        // bUseFaceGenPreprocessedHeads decides whether the engine loads a head that was
        // baked to disk or generates one at runtime. It is cleared only while this
        // client is building an actor whose appearance arrived over the wire, because
        // no baked head can match a face that was never generated here: the lookup
        // finds nothing and the head comes up empty, which is the invisible head a
        // custom race reports - a custom race ships its head as a .nif/.tri and never
        // as FaceGenData. Upstream #764 removed the unconditional per-frame clear this
        // fork used to run, and that clear was the only thing asking for runtime
        // generation; keeping it scoped to wire-built actors is what lets locally
        // generated characters keep the baked head the flag is wanted for (see the
        // comment in TiltedOnlineApp::Update).
        if (m_faceGenForced == aForce)
            return;

        auto* pCollection = INISettingCollection::Get();
        auto* pSetting = pCollection ? pCollection->GetSetting("bUseFaceGenPreprocessedHeads:General") : nullptr;
        if (!pSetting)
        {
            // Null is the engine's answer both for a setting this runtime does not define
            // and for a runtime whose INISettingCollection is not the layout TES.h asserts.
            // The offsets behind that layout were measured on 1.5.97 only, so only this
            // line tells a user report which of the two they are looking at.
            spdlog::warn("{}: bUseFaceGenPreprocessedHeads is not in the engine's ini collection, so the "
                         "preprocessed-head flag cannot be forced for wire-built actors on this runtime",
                         __FUNCTION__);
            return; // nothing to force, and leaving the state alone keeps the next call honest
        }

        if (aForce)
            m_faceGenPreprocessedBackup = pSetting->data;

        pSetting->data = aForce ? 0 : m_faceGenPreprocessedBackup;
        m_faceGenForced = aForce;

        // One line per session, not one per call: the first write is the only one a user
        // log needs, and the pointer and the value it prints are the whole answer to
        // whether the 1.5.97-only offsets in TES.h resolved the setting here. Anything
        // below info is invisible in a shipped build - TiltedOnlineApp leaves
        // set_level(debug) commented out.
        static bool s_reportedFirstForce = false;
        if (!s_reportedFirstForce)
        {
            s_reportedFirstForce = true;
            spdlog::info("{}: bUseFaceGenPreprocessedHeads at {} = {} (was {})", __FUNCTION__, fmt::ptr(pSetting), pSetting->data, m_faceGenPreprocessedBackup);
        }

        spdlog::debug("{}: bUseFaceGenPreprocessedHeads = {} (was {})", __FUNCTION__, pSetting->data, m_faceGenPreprocessedBackup);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::SetRemoteFaceGenForce");
    }
}

void CharacterService::UpdateRemoteFaceGenWindow() noexcept
{
    // The window stays open only while such an actor is still waiting for its body.
    // Deriving it from the components every frame instead of counting opens and
    // closes is deliberate: the actor stops matching the moment its 3D arrives, and
    // a disconnect clears RemoteComponent, so the flag restores itself without
    // anyone having to remember to close it.
    bool needsRuntimeFaceGen = false;

    // RemoteComponent belongs in the predicate: disconnecting clears it without
    // touching WaitingFor3D, so without it the window would stay open past the
    // session that needed it and put every later actor back on runtime generation.
    const auto waitingView = m_world.view<WaitingFor3D, RemoteComponent>();

    for (auto entity : waitingView)
    {
        // An empty FormId is the wire-built path: the base was rebuilt here from the
        // appearance buffer rather than resolved out of the local load order, so its
        // face is not the face those baked assets were generated for.
        if (waitingView.get<WaitingFor3D>(entity).SpawnRequest.FormId == GameId{})
        {
            needsRuntimeFaceGen = true;
            break;
        }
    }

    // There is deliberately no timeout on this. A timer armed when the window opens
    // would already have expired for a second actor that starts waiting later, and
    // closing the window on that actor's first frame is the invisible head again.
    // The state this can leave behind - a body that never materializes keeps the
    // flag cleared - is exactly what shipped as 1.7.1, which is the build this
    // report calls working. RunSpawnUpdates below waits on the same component and
    // has no timeout either.

    SetRemoteFaceGenForce(needsRuntimeFaceGen);
}

void CharacterService::RunRemoteUpdates() noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        // Play the remote world slightly in the past so there is always a sample
        // ahead of the play head to interpolate towards. The margin has to cover
        // one snapshot interval (the client sends every 100ms) plus the relay, so
        // 300ms leaves room for two late snapshots; it is a constant rather than a
        // tuned value because the clock exposes no round-trip or jitter estimate to
        // derive it from.
        //
        // It is expressed against the same clock the samples carry. A server that
        // predates the per-actor sample time stamps each packet with its own relay
        // tick, and on those the margin is what keeps the play head behind it.
        constexpr uint64_t kPlaybackDelayMs = 300;
        const auto cNow = m_transport.GetClock().GetCurrentTick();
        const auto tick = cNow > kPlaybackDelayMs ? cNow - kPlaybackDelayMs : 0;

        // Interpolation has to keep running even if the actor is not in view, otherwise we will never know if we need to spawn it
        auto interpolatedEntities = m_world.view<RemoteComponent, InterpolationComponent>();

        for (auto entity : interpolatedEntities)
        {
            auto* pFormIdComponent = m_world.try_get<FormIdComponent>(entity);
            auto& interpolationComponent = interpolatedEntities.get<InterpolationComponent>(entity);

            Actor* pActor = nullptr;
            if (pFormIdComponent)
            {
                auto* pForm = TESForm::GetById(pFormIdComponent->Id);
                pActor = Cast<Actor>(pForm);
            }

            InterpolationSystem::Update(pActor, interpolationComponent, tick);
        }

        auto animatedView = m_world.view<RemoteComponent, RemoteAnimationComponent, FormIdComponent>();

        for (auto entity : animatedView)
        {
            auto& animationComponent = animatedView.get<RemoteAnimationComponent>(entity);
            auto& formIdComponent = animatedView.get<FormIdComponent>(entity);

            auto* pForm = TESForm::GetById(formIdComponent.Id);
            auto* pActor = Cast<Actor>(pForm);

            // Called even when the actor cannot be resolved: the queue has to be
            // trimmed either way, and the snapshots keep appending to it.
            AnimationSystem::Update(m_world, pActor, animationComponent, tick);
        }

        auto facegenView = m_world.view<FormIdComponent, FaceGenComponent>();

        for (auto entity : facegenView)
        {
            auto& faceGenComponent = facegenView.get<FaceGenComponent>(entity);

            // Checked here as well as inside FaceGenSystem::Update, because the two
            // costs are different: an already generated head costs nothing there,
            // but reaching it still needs a TESForm::GetById and an RTTI-backed
            // Cast per actor per frame. Only the remaining candidates are worth
            // resolving at all.
            if (faceGenComponent.Generated)
                continue;

            auto& formIdComponent = facegenView.get<FormIdComponent>(entity);

            auto* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (!pActor)
                continue;

            FaceGenSystem::Update(m_world, pActor, faceGenComponent);
        }

        auto waitingView = m_world.view<FormIdComponent, WaitingFor3D>();

        Vector<entt::entity> readyEntities;
        for (auto entity : waitingView)
        {
            auto& formIdComponent = waitingView.get<FormIdComponent>(entity);
            auto& waitingFor3D = waitingView.get<WaitingFor3D>(entity);

            Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (!pActor || !pActor->GetNiNode())
                continue;

            // By now, the actor has materialized in the world and is ready for further setup

            pActor->SetActorInventory(waitingFor3D.SpawnRequest.InventoryContent);
            pActor->SetFactions(waitingFor3D.SpawnRequest.FactionsContent);

            if (!waitingFor3D.SpawnRequest.ActionsToReplay.Actions.empty())
            {
                pActor->LoadAnimationVariables(waitingFor3D.SpawnRequest.ActionsToReplay.Actions[0].Variables);
            }

            m_weaponDrawUpdates[pActor->formID] = {waitingFor3D.SpawnRequest.IsWeaponDrawn};

            if (pActor->IsDead() != waitingFor3D.SpawnRequest.IsDead)
                waitingFor3D.SpawnRequest.IsDead ? pActor->Kill() : pActor->Respawn();

            if (pActor->IsVampireLord())
                pActor->FixVampireLordModel();

            // (Re)start naked NPC deadline. Actor MUST be fully constructed before
            // check clock starts. This line is fork-only; upstream's side of the
            // merge had only the readyEntities push below.
            pActor->GetExtension()->SetNakedDeadline();

            // Clearing the flag is only half of it: the value is read inside the
            // engine's own facegen pass, and nothing here ever asked for that pass.
            // Left alone, the engine reaches its head decision on a schedule this
            // client does not control, and the window that holds the flag down is
            // closed on the tick the body appears.
            //
            // Ask for the pass explicitly, while the window is still open. QueueUpdate
            // is the call the engine itself brackets with a save-clear-restore of that
            // flag when it wants a runtime head (SkyrimSE.exe+0x31c1ca), so it is the
            // engine-blessed way to request one. Scoped like the window: an actor whose
            // base came out of the local load order keeps its baked head.
            if (waitingFor3D.SpawnRequest.FormId == GameId{})
                pActor->QueueUpdate();

            readyEntities.push_back(entity);

            spdlog::debug(__FUNCTION__ ": applied 3D for actor, form id: {:X}, name {}", pActor->formID, pActor->baseForm ? pActor->baseForm->GetName() : "<no base form>");
        }

        for (auto entity : readyEntities)
        {
            m_world.remove<WaitingFor3D>(entity);

            // Reprocess the remote actor now that an ownership grant can be accepted without immediately declining it.
            ProcessNewEntity(entity);
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::RunRemoteUpdates");
    }
}

void CharacterService::RunFactionsUpdates() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 2000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    RequestFactionsChanges message;

    auto factionedActors = m_world.view<LocalComponent, CacheComponent, FormIdComponent>();
    for (auto entity : factionedActors)
    {
        auto& formIdComponent = factionedActors.get<FormIdComponent>(entity);
        auto& localComponent = factionedActors.get<LocalComponent>(entity);
        auto& cacheComponent = factionedActors.get<CacheComponent>(entity);

        const auto* pForm = TESForm::GetById(formIdComponent.Id);
        const auto* pActor = Cast<Actor>(pForm);
        if (!pActor)
            continue;

        // Check if cached factions and current factions are identical
        auto factions = pActor->GetFactions();

        if (cacheComponent.FactionsContent == factions)
            continue;

        cacheComponent.FactionsContent = factions;

        // If not send the current factions and replace the cached factions
        message.Changes[localComponent.Id] = factions;
    }

    if (!message.Changes.empty())
        m_transport.Send(message);
}

void CharacterService::RunSpawnUpdates() const noexcept
{
    auto invisibleView = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent, WaitingFor3D>(entt::exclude<FormIdComponent>);
    Vector<entt::entity> entities(invisibleView.begin(), invisibleView.end());

    for (const auto entity : entities)
    {
        auto& remoteComponent = m_world.get<RemoteComponent>(entity);
        auto& interpolationComponent = m_world.get<InterpolationComponent>(entity);

        // The player is null on the main menu and while a save is loading, where
        // this per-frame pass has nothing to compare against.
        PlayerCharacter* pPlayer = PlayerCharacter::Get();
        if (const auto pWorldSpace = pPlayer ? pPlayer->GetWorldSpace() : nullptr)
        {
            float characterX = interpolationComponent.Position.x;
            float characterY = interpolationComponent.Position.y;
            const auto characterCoords = GridCellCoords::CalculateGridCellCoords(characterX, characterY);
            // The game data is not up on the main menu or while a save is loading,
            // where there is no grid to place the remote player in.
            const TES* pTES = TES::Get();
            if (!pTES)
                continue;

            const auto playerCoords = GridCellCoords(pTES->centerGridX, pTES->centerGridY);

            // TODO(cosideci): IsDragon probably shouldn't be straight up false here.
            if (GridCellCoords::IsCellInGridCell(characterCoords, playerCoords, false))
            {
                // A server id that already carries a ref id must never grow a second
                // actor. The create below used to run whenever the lookup failed, and
                // the lookup legitimately fails for a tick or two while the engine
                // finishes registering a freshly created reference. That produced two
                // live actors for one remote player - both then had their 3D updated
                // back to back and the game dereferenced a null in its own reference
                // update (SkyrimSE.exe+0x23d000, rcx=0). Only the first create, the one
                // with no ref id yet, may spawn; a lost actor is replaced through the
                // server's own spawn request instead.
                auto* pActor = Cast<Actor>(TESForm::GetById(remoteComponent.CachedRefId));
                if (!pActor)
                {
                    if (remoteComponent.CachedRefId != 0)
                        continue;

                    pActor = CreateCharacterForEntity(entity);
                    if (!pActor)
                        continue;

                    remoteComponent.CachedRefId = pActor->formID;
                }

                if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
                    pActor->MoveTo(pPlayer->GetParentCellEx(), interpolationComponent.Position);
            }
        }
    }
}

void CharacterService::RunExperienceUpdates() noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        static std::chrono::steady_clock::time_point lastSendTimePoint;
        constexpr auto cDelayBetweenSnapshots = 1000ms;

        const auto now = std::chrono::steady_clock::now();
        if (now - lastSendTimePoint < cDelayBetweenSnapshots)
            return;

        lastSendTimePoint = now;

        if (m_cachedExperience == 0.f)
            return;

        if (!World::Get().GetPartyService().IsInParty())
            return;

        SyncExperienceRequest message;
        message.Experience = m_cachedExperience;

        m_cachedExperience = 0.f;

        m_transport.Send(message);

        spdlog::debug("Sending over experience {}", message.Experience);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::RunExperienceUpdates");
    }
}

void CharacterService::ApplyCachedWeaponDraws(const UpdateEvent& acUpdateEvent) noexcept
{
    // This is the densest noexcept surface in the client: every packet handler, the
    // per-frame remote update pass and the assignment path. Each body allocates -
    // the reports, the maps and vectors the messages carry, the entity lookups.
    // What was written before the failure stays; what is left is either re-sent by
    // the server or recomputed on the next tick.
    try
    {
        std::vector<uint32_t> toRemove{};

        for (auto& [cId, _] : m_weaponDrawUpdates)
        {
            auto& data = m_weaponDrawUpdates[cId];

            data.m_timer += acUpdateEvent.Delta;

            // Remote actors get 2 passes because Skyrim's weapon drawing is the most finnicky thing in existence.
            double maxTime = data.m_isFirstPass ? 0.5 : 2.0;
            if (data.m_timer <= maxTime)
                continue;

            Actor* pActor = Cast<Actor>(TESForm::GetById(cId));
            if (!pActor || !pActor->GetExtension()->IsRemote())
            {
                toRemove.push_back(cId);
                continue;
            }

            pActor->SetWeaponDrawnEx(data.m_drawWeapon);

            if (!data.m_isFirstPass)
                toRemove.push_back(cId);

            data.m_isFirstPass = false;
        }

        for (uint32_t id : toRemove)
            m_weaponDrawUpdates.erase(id);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in CharacterService::ApplyCachedWeaponDraws");
    }
}
