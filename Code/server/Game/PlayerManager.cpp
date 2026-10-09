#include "PlayerManager.h"
#include "Player.h"

static PlayerManager* s_pInstance = nullptr;

PlayerManager::PlayerManager()
{
    s_pInstance = this;
}

PlayerManager::~PlayerManager() noexcept
{
    s_pInstance = nullptr;
}

PlayerManager* PlayerManager::Get() noexcept
{
    // The report formats into a fresh buffer, which is the only allocation here. The
    // answer is null either way, and every caller already handles that: the pointer is
    // the manager itself, which the server built before it could serve anyone.
    try
    {
        if (!s_pInstance)
        {
            spdlog::error("Invalid access to playermanager");
            return nullptr;
        }

        return s_pInstance;
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while reporting a missing player manager");
    }

    return nullptr;
}

Player* PlayerManager::Create(ConnectionId_t aConnectionId) noexcept
{
    // The map node and the Player both allocate. The caller is the authentication
    // path, and it already treats a null answer as "this connection has no player":
    // the request is refused instead of the server dying on a bad_alloc.
    try
    {
        const auto itor = m_players.find(aConnectionId);
        if (itor == std::end(m_players))
        {
            const auto [insertedItor, inserted] = m_players.emplace(aConnectionId, MakeUnique<Player>(aConnectionId));
            if (inserted)
            {
                return insertedItor.value().get();
            }
        }

        return nullptr;
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while creating a player row");
    }

    return nullptr;
}

void PlayerManager::Remove(Player* apPlayer) noexcept
{
    m_players.erase(apPlayer->GetConnectionId());
}

Player* PlayerManager::GetByConnectionId(ConnectionId_t aConnectionId) noexcept
{
    const auto itor = m_players.find(aConnectionId);
    if (itor != std::end(m_players))
    {
        return itor.value().get();
    }

    return nullptr;
}

Player const* PlayerManager::GetByConnectionId(ConnectionId_t aConnectionId) const noexcept
{
    const auto itor = m_players.find(aConnectionId);
    if (itor != std::end(m_players))
    {
        return itor.value().get();
    }

    return nullptr;
}

Player* PlayerManager::GetById(uint32_t aId) noexcept
{
    auto itor = std::begin(m_players);
    const auto end = std::end(m_players);

    for (; itor != end; ++itor)
        if (itor.value()->GetId() == aId)
            return itor.value().get();

    return nullptr;
}

Player const* PlayerManager::GetById(uint32_t aId) const noexcept
{
    auto itor = std::begin(m_players);
    const auto end = std::end(m_players);

    for (; itor != end; ++itor)
        if (itor.value()->GetId() == aId)
            return itor.value().get();

    return nullptr;
}

Player* PlayerManager::GetByUsername(const String& acUsername) const noexcept
{
    auto itor = std::begin(m_players);
    const auto end = std::end(m_players);

    for (; itor != end; ++itor)
        if (itor.value()->GetUsername() == acUsername)
            return itor.value().get();

    return nullptr;
}

Player const* PlayerManager::GetByUsername(const String& acUsername) noexcept
{
    auto itor = std::begin(m_players);
    const auto end = std::end(m_players);

    for (; itor != end; ++itor)
        if (itor.value()->GetUsername() == acUsername)
            return itor.value().get();

    return nullptr;
}

uint32_t PlayerManager::Count() const noexcept
{
    return static_cast<uint32_t>(m_players.size());
}
