#include "LogModel.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace serverui
{

namespace
{
char LowerAscii(char aCh)
{
    return (aCh >= 'A' && aCh <= 'Z') ? static_cast<char>(aCh + ('a' - 'A')) : aCh;
}

bool ContainsFold(const std::string& acHaystack, const std::string& acNeedle, bool aCaseSensitive)
{
    if (acNeedle.empty())
        return true;
    if (acNeedle.size() > acHaystack.size())
        return false;

    const size_t last = acHaystack.size() - acNeedle.size();
    for (size_t i = 0; i <= last; ++i)
    {
        size_t j = 0;
        for (; j < acNeedle.size(); ++j)
        {
            const char a = aCaseSensitive ? acHaystack[i + j] : LowerAscii(acHaystack[i + j]);
            const char b = aCaseSensitive ? acNeedle[j] : LowerAscii(acNeedle[j]);
            if (a != b)
                break;
        }
        if (j == acNeedle.size())
            return true;
    }
    return false;
}
} // namespace

const char* LevelName(LogLevel aLevel)
{
    switch (aLevel)
    {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRIT";
    }
    return "INFO";
}

LogModel::LogModel(size_t aCapacity)
    : m_capacity(aCapacity < 1 ? 1 : aCapacity)
    , m_nextAbs(0)
    , m_stored(0)
{
    m_entries.resize(m_capacity);
    std::memset(&m_scratch, 0, sizeof(m_scratch));
}

void LogModel::Append(const LogRecord& acRecord)
{
    if (m_stored == m_capacity)
        EvictOldest();

    Entry& entry = m_entries[SlotOf(m_nextAbs)];
    entry.seq = acRecord.seq;
    entry.timestampMs = acRecord.timestampMs;
    entry.threadId = acRecord.threadId;
    entry.level = acRecord.level;
    entry.text.assign(acRecord.text != nullptr ? acRecord.text : "", acRecord.text != nullptr ? acRecord.len : 0);

    if (Matches(entry))
        m_visible.push_back(m_nextAbs);

    ++m_nextAbs;
    ++m_stored;
}

void LogModel::AppendBatch(const LogRecord* apRecords, size_t aCount)
{
    if (apRecords == nullptr)
        return;
    for (size_t i = 0; i < aCount; ++i)
        Append(apRecords[i]);
}

void LogModel::EvictOldest()
{
    const uint64_t oldest = m_nextAbs - m_stored;
    while (!m_visible.empty() && m_visible.front() <= oldest)
        m_visible.pop_front();
    --m_stored;
}

bool LogModel::Matches(const Entry& acEntry) const
{
    if (static_cast<int>(acEntry.level) < static_cast<int>(m_filter.minLevel))
        return false;
    return ContainsFold(acEntry.text, m_filter.substring, m_filter.caseSensitive);
}

void LogModel::SetFilter(const LogFilter& acFilter)
{
    m_filter = acFilter;
    RebuildVisible();
}

void LogModel::RebuildVisible()
{
    m_visible.clear();
    const uint64_t oldest = m_nextAbs - m_stored;
    for (uint64_t abs = oldest; abs < m_nextAbs; ++abs)
    {
        if (Matches(EntryAt(abs)))
            m_visible.push_back(abs);
    }
}

const LogRecord* LogModel::Get(size_t aVisibleIndex) const
{
    if (aVisibleIndex >= m_visible.size())
        return nullptr;

    const Entry& entry = EntryAt(m_visible[aVisibleIndex]);
    m_scratch.seq = entry.seq;
    m_scratch.level = entry.level;
    m_scratch.timestampMs = entry.timestampMs;
    m_scratch.threadId = entry.threadId;
    m_scratch.len = static_cast<uint32_t>(entry.text.size());
    m_scratch.text = entry.text.c_str();
    return &m_scratch;
}

bool LogModel::Export(const std::string& acPath, bool aOnlyVisible) const
{
    std::ofstream out(acPath, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    const auto writeEntry = [&out](const Entry& acEntry)
    {
        out << acEntry.seq << '|' << LevelName(acEntry.level) << '|' << acEntry.threadId << '|' << acEntry.text << '\n';
    };

    if (aOnlyVisible)
    {
        for (uint64_t abs : m_visible)
            writeEntry(EntryAt(abs));
    }
    else
    {
        const uint64_t oldest = m_nextAbs - m_stored;
        for (uint64_t abs = oldest; abs < m_nextAbs; ++abs)
            writeEntry(EntryAt(abs));
    }

    return out.good();
}

void LogModel::Clear()
{
    m_visible.clear();
    m_nextAbs = 0;
    m_stored = 0;
    for (Entry& entry : m_entries)
        entry.text.clear();
}

} // namespace serverui
