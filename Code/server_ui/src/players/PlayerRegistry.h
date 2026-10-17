#pragma once

#include "../core/ServerCore.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace serverui
{

// The authoritative list of connected players behind the dashboard's player
// table.
//
// Threading:
//   * the core thread mutates (OnJoin / OnLeave / SetPing / Tick),
//   * any thread may call Snapshot() / EndpointOf() / UsernameOf() / Size().
// One mutex guards the mutate and the copy path; the render path only ever
// touches the vector Snapshot() returned and never takes the lock.
//
// PlayerRow::username is a bare const char*. Names are interned in an
// append-only arena, and a std::deque never invalidates references to its
// elements, so a username pointer handed out by Snapshot() stays valid for the
// LIFETIME OF THE REGISTRY, not merely until the next Snapshot(). The arena
// grows with the number of DISTINCT usernames ever seen - the same trade
// ServerCore already makes for the same reason.
//
// PlayerRow carries no endpoint field (that struct belongs to the core
// contract), so the dashboard's IP column joins Snapshot() with
// EndpointOf(connectionId).
//
// Ping: the server has no RTT measurement today, so a row starts at
// kPingUnknown and stays there until SetPing() reports a real sample. Nothing
// is fabricated.
class PlayerRegistry
{
public:
    // Contract: pingMs == 0 means "no RTT measurement exists".
    static constexpr uint32_t kPingUnknown = 0;

    // A ping sample older than this is dropped back to kPingUnknown by Tick():
    // a stale number is worse than an honest "unknown".
    static constexpr uint64_t kPingStaleMs = 5000;

    PlayerRegistry() = default;
    PlayerRegistry(const PlayerRegistry&) = delete;
    PlayerRegistry& operator=(const PlayerRegistry&) = delete;

    // false when the username is empty, or when the connection is already
    // known (a duplicate join must not overwrite a live row and reset its
    // connectedAtMs).
    bool OnJoin(uint32_t aConnectionId, uint32_t aPlayerId, const std::string& acUsername,
                const std::string& acEndpoint, uint64_t aNowMs, bool aIsAdmin);

    bool OnLeave(uint32_t aConnectionId);

    // aPingMs == 0 stores "unknown". The sample is stamped with the registry
    // clock, so call Tick(nowMs) once per core frame to keep it current.
    bool SetPing(uint32_t aConnectionId, uint32_t aPingMs);
    bool MarkPingUnknown(uint32_t aConnectionId);

    // Advances the registry clock and expires ping samples older than
    // kPingStaleMs.
    void Tick(uint64_t aNowMs);

    // Stable copy for the UI thread, ordered by connectionId so rows do not
    // jump between frames.
    std::vector<PlayerRow> Snapshot() const;

    bool Contains(uint32_t aConnectionId) const;
    bool EndpointOf(uint32_t aConnectionId, std::string* apEndpoint) const;
    bool UsernameOf(uint32_t aConnectionId, std::string* apUsername) const;

    size_t Size() const;
    void Clear();

private:
    struct Entry
    {
        PlayerRow row{};
        std::string endpoint;
        uint64_t pingSampledAtMs{0};
    };

    const char* InternNameLocked(const std::string& acUsername);

    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, Entry> m_entries;
    uint64_t m_nowMs{0};

    std::deque<std::string> m_names;
    std::unordered_map<std::string, const char*> m_nameIndex;
};

} // namespace serverui
