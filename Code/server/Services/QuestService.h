#pragma once

#include <Events/PacketEvent.h>
#include <Structs/GameId.h>

#include <cstdint>

struct World;
struct UpdateEvent;
struct RequestQuestUpdate;
struct RequestQuestSceneUpdate;

/**
 * @brief Dispatch quest sync messages.
 */
class QuestService
{
  public:
    QuestService(World& aWorld, entt::dispatcher& aDispatcher);

  private:
    void OnQuestChanges(const PacketEvent<RequestQuestUpdate>& aChanges) noexcept;
    void OnQuestSceneChanges(const PacketEvent<RequestQuestSceneUpdate>& aChanges) noexcept;

    World& m_world;

    // A quest log row is keyed by a GameId the client picks, and the only thing that ever
    // retires one is a Stopped update for that same id: a client that keeps inventing ids
    // grows this table for as long as it stays connected. Bounded here like every other
    // table whose key the peer picks. A full log refuses the new id instead of growing
    // without limit; updates for ids already in it still go through.
    static constexpr std::uint32_t kMaxQuestLogEntries = 4096;

    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_questSceneUpdateConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_joinConnection;
};
