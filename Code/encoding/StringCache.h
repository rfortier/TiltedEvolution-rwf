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
