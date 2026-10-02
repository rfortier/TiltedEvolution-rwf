#pragma once

#include <Events/PacketEvent.h>

#include <cstdint>

struct World;
struct PlayerLeaveCellEvent;
struct ActivateRequest;
struct LockChangeRequest;
struct AssignObjectsRequest;
struct ScriptAnimationRequest;

/**
 * @brief Manages (interactive) objects and relays interactions with said objects.
 */
class ObjectService
{
public:
    ObjectService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnPlayerLeaveCellEvent(const PlayerLeaveCellEvent& acEvent) noexcept;
    void OnAssignObjectsRequest(const PacketEvent<AssignObjectsRequest>&) noexcept;
    void OnActivate(const PacketEvent<ActivateRequest>&) const noexcept;
    void OnLockChange(const PacketEvent<LockChangeRequest>&) const noexcept;
    void OnScriptAnimationRequest(const PacketEvent<ScriptAnimationRequest>&) noexcept;

    World& m_world;

    // An object entity is created by a request the client sends, and the only
    // place that destroys one is OnPlayerLeaveCellEvent: as long as a single
    // player stays in a cell, its objects survive. The count therefore only
    // grows with the cells that are occupied, and it is bounded here like every
    // other table whose key the peer picks. A full table refuses the new object
    // instead of growing without limit.
    static constexpr std::uint32_t kMaxObjectEntities = 64 * 1024;

    entt::scoped_connection m_leaveCellConnection;
    entt::scoped_connection m_assignObjectConnection;
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_lockChangeConnection;
    entt::scoped_connection m_scriptAnimationConnection;
};
