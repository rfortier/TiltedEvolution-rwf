#include <Components.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestDeathStateChange.h>
#include <Services/ActorValueService.h>
#include <World.h>
#include <GameServer.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>

ActorValueService::ActorValueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_updateHealthConnection = aDispatcher.sink<PacketEvent<RequestActorValueChanges>>().connect<&ActorValueService::OnActorValueChanges>(this);
    m_updateMaxValueConnection = aDispatcher.sink<PacketEvent<RequestActorMaxValueChanges>>().connect<&ActorValueService::OnActorMaxValueChanges>(this);
    m_updateDeltaHealthConnection = aDispatcher.sink<PacketEvent<RequestHealthChangeBroadcast>>().connect<&ActorValueService::OnHealthChangeBroadcast>(this);
    m_deathStateConnection = aDispatcher.sink<PacketEvent<RequestDeathStateChange>>().connect<&ActorValueService::OnDeathStateChange>(this);
}

void ActorValueService::OnActorValueChanges(const PacketEvent<RequestActorValueChanges>& acMessage) const noexcept
{
    // Indexing the map inserts a row when the key is new, and the notification copies
    // the whole map. The keys are already clamped to ActorValues::kMaxActorValueId by
    // the reader, so what is bounded here is the failure, not the size: a rejected
    // actor value is dropped instead of terminating the server on the tick thread.
    try
    {
        auto& message = acMessage.Packet;

        auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

        auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

        if (it == actorValuesView.end() || !actorValuesView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
            return;

        auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
        for (auto& [id, value] : message.Values)
        {
            actorValuesComponent.CurrentActorValues.ActorValuesList[id] = value;
        }

        NotifyActorValueChanges notify;
        notify.OwnershipEpoch = message.OwnershipEpoch;
        notify.Id = acMessage.Packet.Id;
        notify.Values = acMessage.Packet.Values;

        const entt::entity cEntity = static_cast<entt::entity>(message.Id);
        if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while applying actor value changes");
    }
}

void ActorValueService::OnActorMaxValueChanges(const PacketEvent<RequestActorMaxValueChanges>& acMessage) const noexcept
{
    // Same two allocations as the actor value handler above, on the max value map. A
    // dropped broadcast leaves the server's own copy authoritative, which is what the
    // next change resends.
    try
    {
        auto& message = acMessage.Packet;

        auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

        auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

        if (it == actorValuesView.end() || !actorValuesView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
            return;

        auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
        for (auto& [id, value] : message.Values)
        {
            actorValuesComponent.CurrentActorValues.ActorMaxValuesList[id] = value;
        }

        NotifyActorMaxValueChanges notify;
        notify.OwnershipEpoch = message.OwnershipEpoch;
        notify.Id = message.Id;
        notify.Values = message.Values;

        const entt::entity cEntity = static_cast<entt::entity>(message.Id);
        if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while applying actor max value changes");
    }
}

void ActorValueService::OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) const noexcept
{
    // The health entry is the allocation: it happens whether or not the entity was
    // found, because the notification below is sent either way. A lost delta is
    // corrected by the next health change the owner sends.
    try
    {
        auto& message = acMessage.Packet;

        // TODO(cosideci): should server side health not be updated?
        auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

        auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

        if (it != actorValuesView.end())
        {
            auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
            auto currentHealth = actorValuesComponent.CurrentActorValues.ActorValuesList[24];
            actorValuesComponent.CurrentActorValues.ActorValuesList[24] = currentHealth + message.DeltaHealth;
        }

        NotifyHealthChangeBroadcast notify;
        notify.Id = message.Id;
        notify.DeltaHealth = message.DeltaHealth;

        const entt::entity cEntity = static_cast<entt::entity>(message.Id);
        if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while applying a health change");
    }
}

void ActorValueService::OnDeathStateChange(const PacketEvent<RequestDeathStateChange>& acMessage) const noexcept
{
    // The report below formats into a fresh buffer, which is the only allocation in
    // this body; the notification is a fixed size struct. The death state itself is
    // already written above it.
    try
    {
        auto& message = acMessage.Packet;

        auto characterView = m_world.view<CharacterComponent, OwnerComponent>();

        const auto it = characterView.find(static_cast<entt::entity>(message.Id));

        if (it == characterView.end() || !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
            return;

        auto& characterComponent = characterView.get<CharacterComponent>(*it);
        characterComponent.SetDead(message.IsDead);
        spdlog::debug("Updating death state {:x}:{}", message.Id, message.IsDead);

        NotifyDeathStateChange notify;
        notify.OwnershipEpoch = message.OwnershipEpoch;
        notify.Id = message.Id;
        notify.IsDead = message.IsDead;

        const entt::entity cEntity = static_cast<entt::entity>(message.Id);
        if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while reporting a failed broadcast");
    }
}
