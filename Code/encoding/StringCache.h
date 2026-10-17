#pragma once

#include <Messages/StringCacheUpdate.h>

struct StringCache
{
    TP_NOCOPYMOVE(StringCache);

    [[nodiscard]] bool Contains(const TiltedPhoques::String&) const noexcept;

    [[nodiscard]] std::optional<uint32_t> operator[](const TiltedPhoques::String&) const noexcept;
    [[nodiscard]] std::optional<const TiltedPhoques::String> operator[](uint32_t) const noexcept;
    // Returned by Add when the value could not be recorded. No cursor can equal it:
    // StringCache::Deserialize compares the id it gets against its own cursor and
    // stops applying the message when they disagree, which is what has to happen
    // when the cache did not grow.
    static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;

    // How many strings one cache may hold.
    //
    // The cache is global on the server and grows with every name a client sends, and
    // nothing ever removes an entry: an authenticated client that keeps sending names it
    // has not sent before grows it until the process runs out of memory. There is no
    // eviction instead of a bound because an id has to stay valid for the whole session -
    // the cache is a contiguous prefix of the sender's, which is what lets a message name a
    // string by id - so the only answer left is to stop accepting.
    //
    // The names that travel this way are editor ids, tint names and animation names, a few
    // thousand over a long session, so this is far above any real one. It is also four
    // times the widest count field in the protocol, so a cache that reaches it cannot have
    // been filled by an honest client, and it sits far below kInvalidId, so a full cache
    // cannot produce an id that reads as a failure.
    static constexpr uint32_t kMaxCachedStrings = 1 << 18;

    uint32_t Add(const TiltedPhoques::String&) noexcept;
    [[nodiscard]] void AddWanted(const TiltedPhoques::String&) noexcept;
    [[nodiscard]] size_t Size() const noexcept;
    [[nodiscard]] StringCacheUpdate Serialize(uint32_t& aStartId) const noexcept;
    [[nodiscard]] void Deserialize(const StringCacheUpdate& aMessage) noexcept;
    void Clear() noexcept;
    bool ProcessDirty() noexcept;
    void ClearDirty() noexcept;

    static StringCache& Get() noexcept;

private:
    TiltedPhoques::Vector<TiltedPhoques::String> m_idToString;
    mutable TiltedPhoques::Set<TiltedPhoques::String> m_wantedStrings;
    TiltedPhoques::Map<TiltedPhoques::String, uint32_t> m_stringToId;

    StringCache();
};
