#pragma once

/*  With the introduction of party member (not just leader) quest progression, there are a few challenges:
    SendToParty() will cause reflections of the same quest updates from all party members.
    This is because ScopedQuestOverride() doesn't work as intended because QuestServic::OnEvent calls
    happen later on a different thread. We need to prevent these reflections from sending out dups.

    We want the quest progress to be sent out by the Leader. It has already happened for one Member when
    members advance the quest and it is theoretically possible more than one sends progress, so SendToParty
    needs to not send to those who already have it.

    Since we have to track this anyway, if a Member advances quest, we forward it to the Leader for broadcast.
    This means Leader gets the update in QuestService::OnUpdate(), and when the Leader update reflects back, the
    Leader does SendToParty() (with the original Member and the Leadre in the cache, so SendToParty doesn't send
    to them). This enables the Leader to make a centralized decision; it can reject sending out a member update
    that shouldn't be forwarded; it looks like there are a couple of exceptions like that.
*/
struct QuestStageDedupHistory
{
    static constexpr std::chrono::seconds timeout{30s};

    using QuestId = GameId;
    using QuestStage = uint16_t;
    using PlayerId = uint32_t;
    using TimeStamp = std::chrono::time_point<std::chrono::steady_clock>;

    struct Entry
    {
        QuestId questId{};
        QuestStage questStage{};
        PlayerId playerId{};
        TimeStamp timestamp{};
    };

    using Container = std::deque<Entry>;
    using iterator = Container::iterator;
    using const_iterator = Container::const_iterator;

    void Add(QuestId aQuestId, QuestStage aQuestStage, PlayerId aPlayerId,
             TimeStamp aTimeStamp = std::chrono::steady_clock::now());

    const_iterator FindStage(const QuestId& aQuestId, const QuestStage& aQuestStage);
    const_iterator FindStageWPlayerId(const QuestId& aQuestId, const QuestStage& aQuestStage, const PlayerId& aPlayerId);

    void Reset() noexcept { m_Cache.clear(); }
    bool FoundStage(const QuestId& aQuestId, const QuestStage& aQuestStage)
    {
        return FindStage(aQuestId, aQuestStage) != m_Cache.end();
    }
    bool FoundStageWPlayerId(const QuestId& aQuestId, const QuestStage& aQuestStage, const PlayerId& aPlayerId)
    {
        return FindStageWPlayerId(aQuestId, aQuestStage, aPlayerId) != m_Cache.end();
    }

  private:
    void inline Expire();
    Container m_Cache; // Short, time-ordered, duplicates valid (do they ever happen?)
};



struct ServerMessage;
struct Player
{
    Player(ConnectionId_t aConnectionId);
    ~Player() noexcept = default;

    Player(Player&&) noexcept;
    Player& operator=(Player&&) noexcept = default;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    [[nodiscard]] uint32_t GetId() const noexcept { return m_id; }
    [[nodiscard]] ConnectionId_t GetConnectionId() const noexcept { return m_connectionId; }
    [[nodiscard]] std::optional<entt::entity> GetCharacter() const noexcept { return m_character; }
    [[nodiscard]] PartyComponent& GetParty() noexcept { return m_party; }
    [[nodiscard]] const String& GetUsername() const noexcept { return m_username; }
    [[nodiscard]] const String& GetEndPoint() const noexcept { return m_endpoint; }
    [[nodiscard]] const uint64_t GetDiscordId() const noexcept { return m_discordId; }
    [[nodiscard]] const uint32_t GetStringCacheId() const noexcept { return m_stringCacheId; }
    [[nodiscard]] const uint16_t GetLevel() const noexcept { return m_level; }

    [[nodiscard]] CellIdComponent& GetCellComponent() noexcept;
    [[nodiscard]] const CellIdComponent& GetCellComponent() const noexcept;
    [[nodiscard]] QuestLogComponent& GetQuestLogComponent() noexcept;
    [[nodiscard]] const QuestLogComponent& GetQuestLogComponent() const noexcept;

    void SetDiscordId(uint64_t aDiscordId) noexcept;
    void SetEndpoint(String aEndpoint) noexcept;
    void SetUsername(String aUsername) noexcept;
    void SetMods(Vector<String> aMods) noexcept;
    void SetModIds(Vector<uint16_t> aModIds) noexcept;
    void SetCharacter(entt::entity aCharacter) noexcept;
    void SetStringCacheId(uint32_t aStringCacheId) noexcept;
    // TODO(cosideci): update on level up
    void SetLevel(uint16_t aLevel) noexcept;

    void SetCellComponent(const CellIdComponent& aCellComponent) noexcept;

    void Send(const ServerMessage& acServerMessage) const;
    QuestStageDedupHistory m_questStageDedupHistory;
    QuestStageDedupHistory& GetQuestStageDedupHistory() { return m_questStageDedupHistory; }


private:
    uint32_t m_id{0};
    ConnectionId_t m_connectionId;
    std::optional<entt::entity> m_character;
    Vector<String> m_mods;
    Vector<uint16_t> m_modIds;
    uint64_t m_discordId{0};
    String m_endpoint;
    String m_username;
    PartyComponent m_party;
    QuestLogComponent m_questLog;
    CellIdComponent m_cell;
    uint32_t m_stringCacheId{0};
    uint16_t m_level{0};
};
