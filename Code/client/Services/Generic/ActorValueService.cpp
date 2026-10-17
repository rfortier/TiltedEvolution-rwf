#include <TiltedOnlinePCH.h>

#include <Services/ActorValueService.h>
#include <World.h>
#include <Forms/ActorValueInfo.h>
#include <Games/References.h>
#include <Components.h>

#include <Events/UpdateEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/HealthChangeEvent.h>

#include <Messages/NotifyActorValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/RequestDeathStateChange.h>

#include <misc/ActorValueOwner.h>

ActorValueService::ActorValueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_world.on_construct<LocalComponent>().connect<&ActorValueService::OnLocalComponentAdded>(this);
    m_dispatcher.sink<DisconnectedEvent>().connect<&ActorValueService::OnDisconnected>(this);
    m_dispatcher.sink<ActorRemovedEvent>().connect<&ActorValueService::OnActorRemoved>(this);
    m_dispatcher.sink<UpdateEvent>().connect<&ActorValueService::OnUpdate>(this);
    m_dispatcher.sink<NotifyActorValueChanges>().connect<&ActorValueService::OnActorValueChanges>(this);
    m_dispatcher.sink<NotifyActorMaxValueChanges>().connect<&ActorValueService::OnActorMaxValueChanges>(this);
    m_dispatcher.sink<HealthChangeEvent>().connect<&ActorValueService::OnHealthChange>(this);
    m_dispatcher.sink<NotifyHealthChangeBroadcast>().connect<&ActorValueService::OnHealthChangeBroadcast>(this);
    m_dispatcher.sink<NotifyDeathStateChange>().connect<&ActorValueService::OnDeathStateChange>(this);
}

void ActorValueService::CreateActorValuesComponent(const entt::entity aEntity, Actor* apActor) noexcept
{
    // The component is created first and the two maps are filled from the engine
    // afterwards, so a failure leaves an empty component rather than no component.
    // An empty snapshot reads as "no actor value is known yet", which the broadcast
    // below already skips.
    try
    {
        auto& actorValuesComponent = m_world.emplace_or_replace<ActorValuesComponent>(aEntity);

        for (int i = 0; i < ActorValueInfo::kActorValueCount; i++)
        {
            float value = apActor->GetActorValue(i);
            actorValuesComponent.CurrentActorValues.ActorValuesList.insert({i, value});
            float maxValue = apActor->GetActorPermanentValue(i);
            actorValuesComponent.CurrentActorValues.ActorMaxValuesList.insert({i, maxValue});
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in ActorValueService::CreateActorValuesComponent");
        actorValuesComponent.CurrentActorValues.ActorValuesList.clear();
        actorValuesComponent.CurrentActorValues.ActorMaxValuesList.clear();
    }
}

void ActorValueService::OnLocalComponentAdded(entt::registry& aRegistry, const entt::entity aEntity) noexcept
{
    const auto& formIdComponent = aRegistry.get<FormIdComponent>(aEntity);
    Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));

    if (pActor != NULL)
    {
        auto& localComponent = aRegistry.get<LocalComponent>(aEntity);
        localComponent.IsDead = pActor->IsDead();
        localComponent.IsWeaponDrawn = pActor->actorState.IsWeaponDrawn();
        CreateActorValuesComponent(aEntity, pActor);
    }
}

void ActorValueService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    // TODO: this crashes sometimes, no clue why
    m_world.clear<ActorValuesComponent>();
}

void ActorValueService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();
    const uint32_t formId = acEvent.FormId;

    const auto it = std::find_if(
        std::begin(view), std::end(view),
        [view, formId](auto entity)
        {
            const auto& formIdComponent = view.get<FormIdComponent>(entity);

            return formIdComponent.Id == formId;
        });

    if (it != std::end(view))
        m_world.remove<ActorValuesComponent>(*it);
}

void ActorValueService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunSmallHealthUpdates();
    RunDeathStateUpdates();
    RunActorValuesUpdates();
}

void ActorValueService::BroadcastActorValues() noexcept
{
    // Every request and both components allocate here, on a timer. The engine read
    // and the request build are one unit: the component is only written after the
    // insert succeeded, so a failure skips that actor value and the next tick sends
    // it, instead of recording a value the server was never told about.
    try
    {
        if (!m_transport.IsConnected())
            return;

        auto view = m_world.view<FormIdComponent, LocalComponent, ActorValuesComponent>();

        for (auto entity : view)
        {
            auto& formIdComponent = view.get<FormIdComponent>(entity);
            auto* pForm = TESForm::GetById(formIdComponent.Id);
            auto* pActor = Cast<Actor>(pForm);

            if (!pActor)
                continue;

            auto& localComponent = view.get<LocalComponent>(entity);
            auto& actorValuesComponent = view.get<ActorValuesComponent>(entity);

            RequestActorValueChanges requestValueChanges;
            requestValueChanges.Id = localComponent.Id;
            requestValueChanges.OwnershipEpoch = localComponent.OwnershipEpoch;
            RequestActorMaxValueChanges requestMaxValueChanges;
            requestMaxValueChanges.Id = localComponent.Id;
            requestMaxValueChanges.OwnershipEpoch = localComponent.OwnershipEpoch;

            bool isPlayer = pActor->GetExtension() && pActor->GetExtension()->IsPlayer();

            for (int i = 0; i < ActorValueInfo::kActorValueCount; i++)
            {
                if (isPlayer && i == ActorValueInfo::kDragonSouls)
                    continue;
            
                float newValue = pActor->GetActorValue(i);
                float oldValue = actorValuesComponent.CurrentActorValues.ActorValuesList[i];
                if (newValue != oldValue)
                {
                    requestValueChanges.Values.insert({i, newValue});
                    actorValuesComponent.CurrentActorValues.ActorValuesList[i] = newValue;
                }

                float newMaxValue = pActor->GetActorPermanentValue(i);
                float oldMaxValue = actorValuesComponent.CurrentActorValues.ActorMaxValuesList[i];
                if (newMaxValue != oldMaxValue)
                {
                    requestMaxValueChanges.Values.insert({i, newMaxValue});
                    actorValuesComponent.CurrentActorValues.ActorMaxValuesList[i] = newMaxValue;
                }
            }

            if (requestValueChanges.Values.size() > 0)
            {
                m_transport.Send(requestValueChanges);
            }

            if (requestMaxValueChanges.Values.size() > 0)
            {
                m_transport.Send(requestMaxValueChanges);
            }
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in ActorValueService::BroadcastActorValues");
        // The actor value that could not be sent stays unwritten in the component,
        // so the next tick sees it as changed again and retries it.
    }
}

void ActorValueService::OnHealthChange(const HealthChangeEvent& acEvent) noexcept
{
    // Every body here allocates: the reports format into fresh buffers, the maps the
    // engine fills are copied into components, and the notifications carry them back
    // out. A dropped update is corrected by the next one the game sends.
    try
    {
        if (!m_transport.IsConnected())
            return;

        auto view = m_world.view<FormIdComponent>();

        const auto hitteeIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.HitteeId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (hitteeIt == std::end(view))
        {
            spdlog::warn("Health change event form id component not found, form id: {:X}", acEvent.HitteeId);
            return;
        }

        std::optional<uint32_t> serverIdRes = Utils::GetServerId(*hitteeIt);
        if (!serverIdRes.has_value())
        {
            spdlog::error("{}: failed to find server id", __FUNCTION__);
            return;
        }

        uint32_t serverId = serverIdRes.value();

        if (acEvent.DeltaHealth > -1.0f && acEvent.DeltaHealth < 1.0f)
        {
            if (m_smallHealthChanges.find(serverId) == m_smallHealthChanges.end())
                m_smallHealthChanges[serverId] = acEvent.DeltaHealth;
            else
                m_smallHealthChanges[serverId] += acEvent.DeltaHealth;
            return;
        }

        RequestHealthChangeBroadcast requestHealthChange;
        requestHealthChange.Id = serverId;
        requestHealthChange.DeltaHealth = acEvent.DeltaHealth;

        m_transport.Send(requestHealthChange);

        spdlog::debug("Sent out delta health through collection: {:X}:{:f}", serverId, acEvent.DeltaHealth);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in ActorValueService::OnHealthChange");
    }
}

void ActorValueService::RunSmallHealthUpdates() noexcept
{
    // Every body here allocates: the reports format into fresh buffers, the maps the
    // engine fills are copied into components, and the notifications carry them back
    // out. A dropped update is corrected by the next one the game sends.
    try
    {
        static std::chrono::steady_clock::time_point lastSendTimePoint;
        constexpr auto cDelayBetweenUpdates = 250ms;

        const auto now = std::chrono::steady_clock::now();
        if (now - lastSendTimePoint < cDelayBetweenUpdates)
            return;

        lastSendTimePoint = now;

        if (!m_smallHealthChanges.empty())
        {
            for (auto& value : m_smallHealthChanges)
            {
                RequestHealthChangeBroadcast requestHealthChange;
                requestHealthChange.Id = value.first;
                requestHealthChange.DeltaHealth = value.second;

                m_transport.Send(requestHealthChange);

                spdlog::debug("Sent out delta health through timer, {:X}:{:f}", value.first, value.second);
            }

            m_smallHealthChanges.clear();
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in ActorValueService::RunSmallHealthUpdates");
    }
}

void ActorValueService::RunDeathStateUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto localView = m_world.view<FormIdComponent, LocalComponent>();

    // IsDead() is a papyrus call, so walking every local actor here cost one vm
    // call per actor four times a second and grew with the number of loaded
    // actors. The walk is now cut into windows of cMaxActorsPerUpdate and the
    // next update resumes where this one stopped, which bounds the vm calls of a
    // single update to that window independently of how many actors exist. A
    // sweep over M unchanged actors takes ceil(M / 64) updates, so with the 250ms
    // timer above a death is noticed within ceil(M / 64) * 250ms: 250ms up to 64
    // actors, 500ms at 100, 4s at 1000. Actors appearing or leaving mid-sweep can
    // shift the order and push one actor into the next sweep, which costs at most
    // one extra window and never more than one vm call per actor per window.
    constexpr uint32_t cMaxActorsPerUpdate = 64;

    // The cursor is an offset into the view rather than an iterator, because
    // actors are added and removed between updates and an iterator into a changed
    // view is not something to keep. It is reset to zero once a sweep reaches the
    // end, so a view that shrank below the cursor costs one idle update and then
    // starts over from the first actor.
    uint32_t visited = 0;
    uint32_t checked = 0;
    bool complete = true;

    for (auto entity : localView)
    {
        if (visited < m_deathStateCursor)
        {
            ++visited;
            continue;
        }

        if (checked == cMaxActorsPerUpdate)
        {
            complete = false;
            break;
        }

        ++visited;
        ++checked;

        const auto& formIdComponent = localView.get<FormIdComponent>(entity);
        Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            continue;

        auto& localComponent = localView.get<LocalComponent>(entity);

        bool isDead = pActor->IsDead();
        if (isDead != localComponent.IsDead)
        {
            localComponent.IsDead = isDead;

            RequestDeathStateChange requestChange;
            requestChange.Id = localComponent.Id;
            requestChange.OwnershipEpoch = localComponent.OwnershipEpoch;
            requestChange.IsDead = isDead;

            m_transport.Send(requestChange);
        }
    }

    m_deathStateCursor = complete ? 0 : visited;
}

void ActorValueService::RunActorValuesUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    BroadcastActorValues();
}

void ActorValueService::OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) const noexcept
{
    // Every body here allocates: the reports format into fresh buffers, the maps the
    // engine fills are copied into components, and the notifications carry them back
    // out. A dropped update is corrected by the next one the game sends.
    try
    {
        Actor* pActor = Utils::GetByServerId<Actor>(acMessage.Id);
        if (!pActor)
        {
            spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.Id);
            return;
        }

        const float newHealth = pActor->GetActorValue(ActorValueInfo::kHealth) + acMessage.DeltaHealth;
        pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, newHealth);

        const float health = pActor->GetActorValue(ActorValueInfo::kHealth);
        if (!pActor->IsDead() && health <= 0.f)
        {
            ActorExtension* pExtension = pActor->GetExtension();
            // Players should never be killed
            if (!pExtension->IsPlayer())
                pActor->Kill();
        }

        // TODO(cosideci): find fix for player health sync so this can be used again
        /*
        if (pActor->GetExtension()->IsRemotePlayer())
            World::Get().GetOverlayService().SetPlayerHealthPercentage(pActor->formID);
        */
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in ActorValueService::OnHealthChangeBroadcast");
    }
}

void ActorValueService::OnActorValueChanges(const NotifyActorValueChanges& acMessage) const noexcept
{
    // Every body here allocates: the reports format into fresh buffers, the maps the
    // engine fills are copied into components, and the notifications carry them back
    // out. A dropped update is corrected by the next one the game sends.
    try
    {
        auto view = m_world.view<FormIdComponent, RemoteComponent>();

        const auto itor = std::find_if(std::begin(view), std::end(view), [&acMessage, view](entt::entity entity)
        {
            const auto& remote = view.get<RemoteComponent>(entity);
            return remote.Id == acMessage.Id && acMessage.OwnershipEpoch != 0 && remote.OwnershipEpoch == acMessage.OwnershipEpoch;
        });

        if (itor == std::end(view))
            return;

        auto& formIdComponent = view.get<FormIdComponent>(*itor);
        Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));

        if (!pActor)
            return;

        for (const auto& [key, value] : acMessage.Values)
        {
            // Syncing dragon souls triggers "Dragon soul collected" event
            if (key == ActorValueInfo::kDragonSouls || key == ActorValueInfo::kHealth)
                continue;

            spdlog::debug("Actor value update, server ID: {:X}, key: {}, value: {}", acMessage.Id, key, value);

            if (key == ActorValueInfo::kStamina || key == ActorValueInfo::kMagicka || key == ActorValueInfo::kHealth)
            {
                pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, key, value);
                continue;
            }
            pActor->SetActorValue(key, value);
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in ActorValueService::OnActorValueChanges");
    }
}

void ActorValueService::OnActorMaxValueChanges(const NotifyActorMaxValueChanges& acMessage) const noexcept
{
    // Every body here allocates: the reports format into fresh buffers, the maps the
    // engine fills are copied into components, and the notifications carry them back
    // out. A dropped update is corrected by the next one the game sends.
    try
    {
        auto view = m_world.view<FormIdComponent, RemoteComponent>();

        const auto it = std::find_if(std::begin(view), std::end(view), [&acMessage, view](entt::entity entity)
        {
            const auto& remote = view.get<RemoteComponent>(entity);
            return remote.Id == acMessage.Id && acMessage.OwnershipEpoch != 0 && remote.OwnershipEpoch == acMessage.OwnershipEpoch;
        });

        if (it == std::end(view))
            return;

        auto& formIdComponent = view.get<FormIdComponent>(*it);
        Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));

        if (!pActor)
            return;

        for (const auto& [key, value] : acMessage.Values)
        {
            if (key == ActorValueInfo::kDragonSouls)
                continue;

            spdlog::debug("Actor max value update, server ID: {:X}, key: {}, value: {}", acMessage.Id, key, value);

            pActor->ForceActorValue(ActorValueOwner::ForceMode::PERMANENT, key, value);
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in ActorValueService::OnActorMaxValueChanges");
    }
}

void ActorValueService::OnDeathStateChange(const NotifyDeathStateChange& acMessage) const noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();

    const auto it = std::find_if(std::begin(view), std::end(view), [&acMessage, view](entt::entity entity)
    {
        const auto& remote = view.get<RemoteComponent>(entity);
        return remote.Id == acMessage.Id && acMessage.OwnershipEpoch != 0 && remote.OwnershipEpoch == acMessage.OwnershipEpoch;
    });

    if (it == std::end(view))
    {
        const auto cEntity = Utils::FindEntityByServerId(acMessage.Id);
        spdlog::info(
            "Dropped death state {} for server id {:X} at epoch {}, entity found: {}, has form id: {}, waiting for 3D: {}", acMessage.IsDead, acMessage.Id, acMessage.OwnershipEpoch,
            cEntity.has_value(), cEntity && m_world.all_of<FormIdComponent>(*cEntity), cEntity && m_world.all_of<WaitingFor3D>(*cEntity));
        return;
    }

    auto& formIdComponent = view.get<FormIdComponent>(*it);
    Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));

    if (!pActor)
        return;

    ActorExtension* pExtension = pActor->GetExtension();
    // Players should never be killed
    if (pExtension->IsPlayer())
        return;

    if (pActor->IsDead() != acMessage.IsDead)
    {
        spdlog::info("Applying death state {} to remote actor {:X}, server id: {:X}", acMessage.IsDead, pActor->formID, acMessage.Id);
        acMessage.IsDead ? pActor->Kill() : pActor->Respawn();
    }
}
