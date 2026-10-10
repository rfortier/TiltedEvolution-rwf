#include <StringCache.h>
#include <algorithm>
#include <iostream>

bool StringCache::Contains(const TiltedPhoques::String& acValue) const noexcept
{
    return m_stringToId.contains(acValue);
}

std::optional<uint32_t> StringCache::operator[](const TiltedPhoques::String& acValue) const noexcept
{
    if (const auto itor = m_stringToId.find(acValue); itor != std::end(m_stringToId))
        return itor->second;

    return std::nullopt;
}

std::optional<const TiltedPhoques::String> StringCache::operator[](uint32_t aValue) const noexcept
{
    if (aValue < m_idToString.size())
        return m_idToString[aValue];

    return std::nullopt;
}

uint32_t StringCache::Add(const TiltedPhoques::String& acValue) noexcept
{
    // Both containers allocate, and this is reached from Deserialize below, which
    // is noexcept. The map write is undone so the cache stays a contiguous prefix
    // of the sender's, and the id returned is one no cursor can equal: Deserialize
    // compares it with its own cursor and drops the rest of the message, which is
    // what it already does for a value that reuses an old id.
    try
    {
        if (auto id = this->operator[](acValue))
            return *id;

        const auto allocatedId = m_idToString.size();
        m_stringToId[acValue] = allocatedId & 0xFFFFFFFF;
        m_idToString.push_back(acValue);

        return allocatedId;
    }
    catch (...)
    {
        m_stringToId.erase(acValue);
        return kInvalidId;
    }
}

void StringCache::AddWanted(const TiltedPhoques::String& acValue) noexcept
{
    // The set insert is the allocation. A name that could not be recorded is simply
    // not wanted: it is asked for again the next time the engine reads it.
    try
    {
        m_wantedStrings.insert(acValue);
    }
    catch (...)
    {
    }
}

size_t StringCache::Size() const noexcept
{
    return m_idToString.size();
}

StringCacheUpdate StringCache::Serialize(uint32_t& aStartId) const noexcept
{
    StringCacheUpdate update;
    update.StartId = aStartId;

    // The slice is the allocation. The cursor is left where it was - it is only
    // advanced after the insert succeeds - so the next call sends the same slice
    // again instead of skipping it, and an empty update carries the start id the
    // receiver checks its own cursor against.
    try
    {
        if (aStartId < m_idToString.size())
        {
            // One message carries at most kMaxValueCount entries, and the receiver
            // requires the ids to be contiguous: a message whose start id does not line
            // up is dropped. Send a slice and advance the cursor by exactly what was
            // written, so the rest goes out on the next call instead of being skipped
            // and leaving the two caches out of step.
            const auto count = std::min<size_t>(m_idToString.size() - aStartId, StringCacheUpdate::kMaxValueCount);

            auto itor = std::begin(m_idToString);
            std::advance(itor, aStartId);

            auto end = itor;
            std::advance(end, count);

            update.Values.insert(std::begin(update.Values), itor, end);

            aStartId += static_cast<uint32_t>(count);
        }
    }
    catch (...)
    {
        update.Values.clear();
    }

    return update;
}

void StringCache::Deserialize(const StringCacheUpdate& aMessage) noexcept
{
    // The sender numbers these entries with its own cursor, and that cursor is a field of
    // the packet: nothing here chose it and nothing here checks it. It used to be an
    // assert, which aborts the process - this frame is noexcept, so there is no way to
    // fail softly, and the default build is releasedbg, where the assert is live. It is
    // the same defect CachedString.cpp had: an id the peer picked, tested with an assert.
    //
    // A message that does not line up is dropped instead. Both checks sit before the
    // mutation they guard, so returning leaves a cache that is still a contiguous prefix
    // of the sender's: the entries already applied landed at exactly their own ids, and
    // Add returns early without touching anything when the value is already known. An id
    // the cache does not hold still reads as "not one of the names I know" - the consumers
    // of a cached name already test it against a fixed list and fall through when it
    // matches nothing.
    if (aMessage.StartId != m_idToString.size())
        return;

    for (auto& value : aMessage.Values)
    {
        const auto expectedId = m_idToString.size();
        const auto id = Add(value);

        // A value that is already in the cache reuses its old id, so a duplicate inside
        // one message does not land at the end either. That is the same disagreement.
        if (id != expectedId)
            return;
    }
}

void StringCache::Clear() noexcept
{
    m_idToString.clear();
    m_wantedStrings.clear();
    m_stringToId.clear();
}

bool StringCache::ProcessDirty() noexcept
{
    if (m_wantedStrings.empty())
        return false;

    for (auto& s : m_wantedStrings)
        Add(s);

    ClearDirty();

    return true;
}

void StringCache::ClearDirty() noexcept
{
    m_wantedStrings.clear();
}

StringCache& StringCache::Get() noexcept
{
    TiltedPhoques::ScopedAllocator _{TiltedPhoques::Allocator::GetDefault()};
    {
        static StringCache s_instance;
        return s_instance;
    }
}

StringCache::StringCache()
{
}
