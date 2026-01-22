#pragma once

#include <World.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>

struct NotifyQuestUpdate;

struct TESQuest;

/**
 * @brief Handles quest sync
 *
 * This service is currently not in use.
 */
class QuestService final : public BSTEventSink<TESQuestStartStopEvent>, BSTEventSink<TESQuestStageEvent>
{
public:
    QuestService(World&, entt::dispatcher&);
    ~QuestService() = default;

    static bool IsNonSyncableQuest(TESQuest* apQuest);
    static void DebugDumpQuests();
    static bool StopQuest(uint32_t aformId);
    const uint32_t PlayerId() const noexcept { return m_playerId; }

private:
    friend struct QuestEventHandler;

    void OnConnected(const ConnectedEvent&) noexcept;
    void Disconnected(const DisconnectedEvent&) noexcept { m_playerId = 0; }


    BSTEventResult OnEvent(const TESQuestStartStopEvent*, const EventDispatcher<TESQuestStartStopEvent>*) override;
    BSTEventResult OnEvent(const TESQuestStageEvent*, const EventDispatcher<TESQuestStageEvent>*) override;

    void OnQuestUpdate(const NotifyQuestUpdate&) noexcept;

    World& m_world;
    uint32_t m_playerId;

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_leftConnection;
    entt::scoped_connection m_questUpdateConnection;
};

class SceneService final : public BSTEventSink<TESSceneEvent>
{
  public:
    SceneService(World&, entt::dispatcher&);
    ~SceneService() = default;

    const uint32_t PlayerId() const noexcept { return m_playerId; }

  private:
    friend struct SceneEventHandler;

    void OnConnected(const ConnectedEvent&) noexcept;
    void Disconnected(const DisconnectedEvent&) noexcept { m_playerId = 0; }

    BSTEventResult OnEvent(const TESSceneEvent*, const EventDispatcher<TESSceneEvent>*) override;

    World& m_world;
    uint32_t m_playerId;

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_leftConnection;
};
