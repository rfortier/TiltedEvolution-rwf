#pragma once

#include "LogRing.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace serverui
{

struct LogFilter
{
    LogLevel minLevel{LogLevel::Trace};
    std::string substring;
    bool caseSensitive{false};
};

const char* LevelName(LogLevel aLevel);

// UI-side, framework-agnostic model of the captured log.
//
// Records live in a fixed-capacity ring (oldest dropped on overflow) and own
// their text, because LogRing::LogRecord::text is only valid until the next
// Drain(). A parallel deque holds the absolute indices of records that pass the
// filter, so a virtualised view can map visibleIndex -> record in O(1) without
// rebuilding anything per frame. Appending is O(1) amortised: a record is
// either pushed to the visible deque or skipped; evictions pop at most one
// visible entry each.
class LogModel
{
public:
    explicit LogModel(size_t aCapacity = 100000);

    void AppendBatch(const LogRecord* apRecords, size_t aCount);
    void Append(const LogRecord& acRecord);

    void SetFilter(const LogFilter& acFilter);
    const LogFilter& Filter() const { return m_filter; }

    // Number of records currently passing the filter (the view's row count).
    size_t Size() const { return m_visible.size(); }

    // Returns nullptr when aVisibleIndex >= Size(). The pointer is valid only
    // until the next Get() call (it points at one shared scratch record).
    const LogRecord* Get(size_t aVisibleIndex) const;

    size_t StoredCount() const { return m_stored; }
    size_t Capacity() const { return m_capacity; }

    bool Export(const std::string& acPath, bool aOnlyVisible) const;

    void Clear();

private:
    struct Entry
    {
        uint64_t seq{0};
        uint64_t timestampMs{0};
        uint32_t threadId{0};
        LogLevel level{LogLevel::Trace};
        std::string text;
    };

    bool Matches(const Entry& acEntry) const;
    size_t SlotOf(uint64_t aAbsIndex) const { return static_cast<size_t>(aAbsIndex % m_capacity); }
    const Entry& EntryAt(uint64_t aAbsIndex) const { return m_entries[SlotOf(aAbsIndex)]; }
    void EvictOldest();
    void RebuildVisible();

    std::vector<Entry> m_entries;
    std::deque<uint64_t> m_visible;
    size_t m_capacity;
    uint64_t m_nextAbs;   // absolute index the next appended record receives
    size_t m_stored;
    LogFilter m_filter;
    mutable LogRecord m_scratch; // backs Get()
};

} // namespace serverui
