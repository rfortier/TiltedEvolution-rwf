#include "PlayerRegistry.h"

#include <algorithm>
#include <utility>

namespace serverui
{

const char* PlayerRegistry::InternNameLocked(const std::string& acUsername)
{
    const auto it = m_nameIndex.find(acUsername);
    if (it != m_nameIndex.end())
        return it->second;

    m_names.push_back(acUsername);
    const char* pName = m_names.back().c_str();
    m_nameIndex.emplace(acUsername, pName);
    return pName;
}

bool PlayerRegistry::OnJoin(uint32_t aConnectionId, uint32_t aPlayerId, const std::string& acUsername,
                            const std::string& acEndpoint, uint64_t aNowMs, bool aIsAdmin)
{
    if (acUsername.empty())
        return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_entries.find(aConnectionId) != m_entries.end())
        return false;

    Entry entry;
    entry.row.connectionId = aConnectionId;
    entry.row.playerId = aPlayerId;
    entry.row.username = InternNameLocked(acUsername);
    entry.row.pingMs = kPingUnknown;
    entry.row.connectedAtMs = aNowMs;
    entry.row.isAdmin = aIsAdmin;
    entry.endpoint = acEndpoint;
    entry.pingSampledAtMs = 0;

    m_entries.emplace(aConnectionId, std::move(entry));

    if (aNowMs > m_nowMs)
        m_nowMs = aNowMs;

    return true;
}

bool PlayerRegistry::OnLeave(uint32_t aConnectionId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.erase(aConnectionId) != 0;
}

bool PlayerRegistry::SetPing(uint32_t aConnectionId, uint32_t aPingMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto it = m_entries.find(aConnectionId);
    if (it == m_entries.end())
        return false;

    it->second.row.pingMs = aPingMs;
    it->second.pingSampledAtMs = m_nowMs;
    return true;
}

bool PlayerRegistry::MarkPingUnknown(uint32_t aConnectionId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto it = m_entries.find(aConnectionId);
    if (it == m_entries.end())
        return false;

    it->second.row.pingMs = kPingUnknown;
    it->second.pingSampledAtMs = 0;
    return true;
}

void PlayerRegistry::Tick(uint64_t aNowMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nowMs = aNowMs;

    for (auto& pair : m_entries)
    {
        Entry& entry = pair.second;
        if (entry.row.pingMs == kPingUnknown)
            continue;

        // pingSampledAtMs == 0 means the sample was recorded before the clock
        // was ever advanced, so its age is unknown rather than infinite: a
        // sample that is not stale must not be thrown away just because Tick()
        // has not run yet.
        if (entry.pingSampledAtMs == 0)
            continue;

        if (aNowMs > entry.pingSampledAtMs && aNowMs - entry.pingSampledAtMs > kPingStaleMs)
        {
            entry.row.pingMs = kPingUnknown;
            entry.pingSampledAtMs = 0;
        }
    }
}

std::vector<PlayerRow> PlayerRegistry::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<PlayerRow> rows;
    rows.reserve(m_entries.size());
    for (const auto& pair : m_entries)
        rows.push_back(pair.second.row);

    std::sort(rows.begin(), rows.end(),
              [](const PlayerRow& aLhs, const PlayerRow& aRhs) { return aLhs.connectionId < aRhs.connectionId; });
    return rows;
}

bool PlayerRegistry::Contains(uint32_t aConnectionId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.find(aConnectionId) != m_entries.end();
}

bool PlayerRegistry::EndpointOf(uint32_t aConnectionId, std::string* apEndpoint) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto it = m_entries.find(aConnectionId);
    if (it == m_entries.end())
        return false;

    if (apEndpoint != nullptr)
        *apEndpoint = it->second.endpoint;
    return true;
}

bool PlayerRegistry::UsernameOf(uint32_t aConnectionId, std::string* apUsername) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto it = m_entries.find(aConnectionId);
    if (it == m_entries.end())
        return false;

    if (apUsername != nullptr && it->second.row.username != nullptr)
        *apUsername = it->second.row.username;
    return true;
}

size_t PlayerRegistry::Size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

void PlayerRegistry::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

} // namespace serverui
