#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace serverui
{

// One ban record. A record matches a player when EITHER its endpoint or its
// username matches, so both fields are optional - but a record with neither set
// matches everybody and is therefore rejected by Add().
struct BanEntry
{
    std::string endpoint;   // IP:port as Player::GetEndPoint() reports it, or "" for none
    std::string username;   // "" for none
    uint64_t expiresAtMs{0}; // 0 == permanent
    std::string reason;
    uint64_t createdAtMs{0};

    bool IsPermanent() const noexcept { return expiresAtMs == 0; }
    bool IsExpired(uint64_t aNowMs) const noexcept { return expiresAtMs != 0 && aNowMs >= expiresAtMs; }
};

// The persistent ban store the dedicated server does not have today.
//
// Identity: a record is keyed by its endpoint when that is set, otherwise by its
// username. Add() replaces the record with the same key, so banning twice
// updates the reason/expiry instead of stacking duplicates.
//
// File format (Save / Load), one record per line, five TAB separated fields:
//
//     <endpoint>\t<username>\t<expiresAtMs>\t<createdAtMs>\t<reason>\n
//
//   * endpoint, username and reason are raw text; TAB, CR and LF cannot appear
//     in them because Load() splits on TAB and lines are LF terminated. A field
//     that needs them is escaped on write: TAB -> \\t, LF -> \\n, CR -> \\r,
//     backslash -> \\\\, so the record count is preserved exactly.
//   * expiresAtMs and createdAtMs are unsigned decimal; 0 means permanent.
//   * a line whose field count is not exactly five, or whose numeric fields do
//     not parse, is SKIPPED - a corrupt file costs the bad records, never the
//     load and never the process.
//   * blank lines and lines whose first character is '#' are comments.
//   * a UTF-8 byte order mark on the first line is ignored.
//
// Threading: same contract as PlayerRegistry - one mutex on the mutate and copy
// paths, the UI renders from the vector Snapshot() returned.
class BanList
{
public:
    // Longest reason kept; longer input is truncated rather than rejected.
    static constexpr size_t kMaxReasonBytes = 512;

    // BanEntry with no endpoint and no username is rejected.
    bool Add(const BanEntry& acEntry);

    // true when a record was removed. aKey matches an entry's endpoint or its
    // username.
    bool Remove(const std::string& acKey);

    // A ban matches when its endpoint equals acEndpoint (non-empty) or its
    // username equals acUsername (non-empty, case sensitive). An expired record
    // never matches. When apReason is not null it receives the matching reason.
    bool IsBanned(const std::string& acEndpoint, const std::string& acUsername, uint64_t aNowMs,
                  std::string* apReason = nullptr) const;

    // Drops expired records. Returns how many were removed.
    size_t Purge(uint64_t aNowMs);

    size_t Size() const;
    void Clear();
    std::vector<BanEntry> Snapshot() const;

    // Writes the whole list, atomically replacing acPath (written to
    // acPath + ".tmp" first). false when the file could not be written.
    bool Save(const std::string& acPath) const;

    // Merges the records read from acPath into this list, replacing same-key
    // entries. Returns the number of records accepted; malformed lines are
    // skipped and do not affect the return value's meaning.
    size_t Load(const std::string& acPath);

    // The two formats helpers, exposed so the test can exercise them directly.
    static std::string EncodeEntry(const BanEntry& acEntry);
    static bool DecodeEntry(const std::string& acLine, BanEntry* apEntry);
    static std::string EscapeField(const std::string& acValue);
    static std::string UnescapeField(const std::string& acValue);

private:
    // Endpoint when set, otherwise username.
    static std::string KeyOf(const BanEntry& acEntry);
    // Every key a stored entry can be removed by.
    static bool MatchesKey(const BanEntry& acEntry, const std::string& acKey);

    mutable std::mutex m_mutex;
    std::vector<BanEntry> m_entries;
};

} // namespace serverui
