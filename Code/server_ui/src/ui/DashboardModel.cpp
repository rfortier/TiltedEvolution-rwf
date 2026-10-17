#include "DashboardModel.h"

#include <cstdio>

namespace serverui
{

namespace
{
std::string Trim(std::string_view aText)
{
    const auto isSpace = [](char aCh)
    {
        return aCh == ' ' || aCh == '\t' || aCh == '\r' || aCh == '\n';
    };

    size_t begin = 0;
    size_t end = aText.size();
    while (begin < end && isSpace(aText[begin]))
        ++begin;
    while (end > begin && isSpace(aText[end - 1]))
        --end;
    return std::string(aText.substr(begin, end - begin));
}

std::string FormatFixed(double aValue, int aDecimals)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", aDecimals, aValue);
    return std::string(buf);
}

std::string Pad2(uint64_t aValue)
{
    std::string text = std::to_string(aValue);
    return text.size() < 2 ? "0" + text : text;
}

} // namespace

size_t MaxScrollOffset(size_t aTotalRows, size_t aViewportRows) noexcept
{
    if (aTotalRows == 0 || aViewportRows == 0)
        return 0;
    const size_t viewport = aViewportRows < aTotalRows ? aViewportRows : aTotalRows;
    return aTotalRows - viewport;
}

RowRange ComputeVisibleRange(size_t aTotalRows, size_t aViewportRows, size_t aScrollOffset) noexcept
{
    RowRange range;
    if (aTotalRows == 0 || aViewportRows == 0)
        return range;

    const size_t viewport = aViewportRows < aTotalRows ? aViewportRows : aTotalRows;
    const size_t maxOffset = aTotalRows - viewport;
    const size_t offset = aScrollOffset < maxOffset ? aScrollOffset : maxOffset;

    range.first = offset;
    range.last = offset + viewport;
    return range;
}

std::string FormatUptime(uint64_t aMs)
{
    const uint64_t totalSec = aMs / 1000;
    const uint64_t days = totalSec / 86400;
    const uint64_t hours = (totalSec / 3600) % 24;
    const uint64_t minutes = (totalSec / 60) % 60;
    const uint64_t seconds = totalSec % 60;

    if (days > 0)
        return std::to_string(days) + "d " + Pad2(hours) + "h " + Pad2(minutes) + "m " + Pad2(seconds) + "s";
    if (hours > 0)
        return std::to_string(hours) + "h " + Pad2(minutes) + "m " + Pad2(seconds) + "s";
    if (minutes > 0)
        return std::to_string(minutes) + "m " + Pad2(seconds) + "s";
    return std::to_string(seconds) + "s";
}

std::string FormatPing(uint32_t aPingMs)
{
    if (aPingMs == 0)
        return "-";
    return std::to_string(aPingMs);
}

std::string FormatPercentOrDash(double aPercent)
{
    if (aPercent <= 0.0)
        return "-";
    return FormatFixed(aPercent, 1) + "%";
}

std::string FormatBytesOrDash(uint64_t aBytes)
{
    if (aBytes == 0)
        return "-";

    constexpr double kKb = 1024.0;
    const double value = static_cast<double>(aBytes);
    if (value < kKb)
        return std::to_string(aBytes) + " B";
    if (value < kKb * kKb)
        return FormatFixed(value / kKb, 1) + " KB";
    if (value < kKb * kKb * kKb)
        return FormatFixed(value / (kKb * kKb), 1) + " MB";
    return FormatFixed(value / (kKb * kKb * kKb), 2) + " GB";
}

std::string FormatRateOrDash(uint64_t aBytesPerSec)
{
    if (aBytesPerSec == 0)
        return "-";

    constexpr double kKb = 1024.0;
    const double value = static_cast<double>(aBytesPerSec);
    if (value < kKb)
        return std::to_string(aBytesPerSec) + " B/s";
    if (value < kKb * kKb)
        return FormatFixed(value / kKb, 1) + " KB/s";
    return FormatFixed(value / (kKb * kKb), 1) + " MB/s";
}

std::string FormatCountOrDash(uint64_t aCount)
{
    if (aCount == 0)
        return "-";
    return std::to_string(aCount);
}

DashboardModel::DashboardModel(LogModel& aLog, size_t aSparkCapacity)
    : m_log(aLog)
    , m_tpsHistory(aSparkCapacity)
    , m_frameTimeHistory(aSparkCapacity)
{
}

void DashboardModel::Update(const CoreSnapshot& acSnapshot, uint64_t aNowMs)
{
    m_snapshot = acSnapshot;
    m_nowMs = aNowMs;

    m_tpsHistory.Push(acSnapshot.tps);
    m_frameTimeHistory.Push(acSnapshot.frameTimeMs);

    const uint64_t packets = acSnapshot.packetsIn + acSnapshot.packetsOut;
    if (!m_havePrevPackets)
    {
        m_packetsPerSec = 0.0;
        m_havePrevPackets = true;
    }
    else if (packets >= m_prevPackets && aNowMs > m_prevPacketsMs)
    {
        const double deltaSec = static_cast<double>(aNowMs - m_prevPacketsMs) / 1000.0;
        m_packetsPerSec = static_cast<double>(packets - m_prevPackets) / deltaSec;
    }
    else
    {
        // Counter reset, or the clock stepped backwards: report nothing rather
        // than a negative or absurd rate.
        m_packetsPerSec = 0.0;
    }

    m_prevPackets = packets;
    m_prevPacketsMs = aNowMs;
}

void DashboardModel::SetPlayers(const std::vector<PlayerRow>& acPlayers)
{
    m_players = acPlayers;
}

std::string DashboardModel::UptimeText() const
{
    return FormatUptime(m_snapshot.uptimeMs);
}

const char* DashboardModel::StateLabel() const
{
    // Mirrors serverui::ToString(RunState) without dragging the core TU into
    // this model's link line.
    switch (m_snapshot.state)
    {
    case RunState::Stopped: return "Stopped";
    case RunState::Starting: return "Starting";
    case RunState::Running: return "Running";
    case RunState::Stopping: return "Stopping";
    }
    return "Unknown";
}

StateColor DashboardModel::StateColorOf() const
{
    switch (m_snapshot.state)
    {
    case RunState::Stopped: return StateColor::Gray;
    case RunState::Starting: return StateColor::Amber;
    case RunState::Running: return StateColor::Green;
    case RunState::Stopping: return StateColor::Red;
    }
    return StateColor::Gray;
}

std::string DashboardModel::PortText() const
{
    return m_snapshot.port == 0 ? "-" : std::to_string(m_snapshot.port);
}

std::string DashboardModel::PlayerCountText() const
{
    return std::to_string(m_snapshot.playerCount);
}

std::string DashboardModel::TpsText() const
{
    if (m_snapshot.tps <= 0.0)
        return "-";
    return FormatFixed(m_snapshot.tps, 1);
}

std::string DashboardModel::FrameTimeText() const
{
    if (m_snapshot.frameTimeMs <= 0.0)
        return "-";
    return FormatFixed(m_snapshot.frameTimeMs, 1) + " ms";
}

std::string DashboardModel::CpuText() const
{
    return FormatPercentOrDash(m_snapshot.cpuPercent);
}

std::string DashboardModel::MemText() const
{
    return FormatBytesOrDash(m_snapshot.memRssBytes);
}

std::string DashboardModel::NetInText() const
{
    return FormatRateOrDash(m_snapshot.netInBytesPerSec);
}

std::string DashboardModel::NetOutText() const
{
    return FormatRateOrDash(m_snapshot.netOutBytesPerSec);
}

std::string DashboardModel::PacketsInText() const
{
    return FormatCountOrDash(m_snapshot.packetsIn);
}

std::string DashboardModel::PacketsOutText() const
{
    return FormatCountOrDash(m_snapshot.packetsOut);
}

std::string DashboardModel::PacketsPerSecText() const
{
    if (!m_havePrevPackets)
        return "-";
    return FormatFixed(m_packetsPerSec, 0) + "/s";
}

PlayerCell DashboardModel::PlayerCellAt(size_t aIndex) const
{
    PlayerCell cell;
    if (aIndex >= m_players.size())
    {
        cell.id = "-";
        cell.username = "-";
        cell.ping = "-";
        cell.duration = "-";
        cell.role = "-";
        return cell;
    }

    const PlayerRow& row = m_players[aIndex];
    cell.id = std::to_string(row.connectionId);
    cell.playerId = row.playerId;
    cell.username = (row.username != nullptr && row.username[0] != '\0') ? std::string(row.username) : std::string("-");
    cell.ping = FormatPing(row.pingMs);
    cell.duration = FormatUptime(m_nowMs > row.connectedAtMs ? m_nowMs - row.connectedAtMs : 0);
    cell.isAdmin = row.isAdmin;
    cell.role = row.isAdmin ? "admin" : "player";
    return cell;
}

LogColor DashboardModel::LogColorOf(LogLevel aLevel) noexcept
{
    switch (aLevel)
    {
    case LogLevel::Trace: return LogColor::Muted;
    case LogLevel::Debug: return LogColor::Subtle;
    case LogLevel::Info: return LogColor::Normal;
    case LogLevel::Warn: return LogColor::Warning;
    case LogLevel::Error: return LogColor::Danger;
    case LogLevel::Critical: return LogColor::Fatal;
    }
    return LogColor::Normal;
}

void DashboardModel::SetLogKeyword(std::string_view aKeyword)
{
    m_filter.substring.assign(aKeyword.data(), aKeyword.size());
    m_log.SetFilter(m_filter);
}

void DashboardModel::SetLogMinLevel(LogLevel aLevel)
{
    m_filter.minLevel = aLevel;
    m_log.SetFilter(m_filter);
}

void DashboardModel::SetLogCaseSensitive(bool aCaseSensitive)
{
    m_filter.caseSensitive = aCaseSensitive;
    m_log.SetFilter(m_filter);
}

void DashboardModel::SetLogViewportRows(size_t aRows)
{
    m_logViewportRows = aRows;
    const size_t maxOffset = MaxScrollOffset(m_log.Size(), m_logViewportRows);
    if (m_logScrollOffset > maxOffset)
        m_logScrollOffset = maxOffset;
}

void DashboardModel::SetLogAutoScroll(bool aEnabled)
{
    m_logAutoScroll = aEnabled;
    if (aEnabled)
        m_logScrollOffset = MaxScrollOffset(m_log.Size(), m_logViewportRows);
}

void DashboardModel::ScrollLogTo(size_t aOffset)
{
    const size_t maxOffset = MaxScrollOffset(m_log.Size(), m_logViewportRows);
    m_logScrollOffset = aOffset < maxOffset ? aOffset : maxOffset;
    m_logAutoScroll = (m_logScrollOffset == maxOffset);
}

void DashboardModel::ScrollLogBy(long long aDeltaRows)
{
    const size_t maxOffset = MaxScrollOffset(m_log.Size(), m_logViewportRows);

    long long next = static_cast<long long>(m_logScrollOffset) + aDeltaRows;
    if (next < 0)
        next = 0;
    if (static_cast<size_t>(next) > maxOffset)
        next = static_cast<long long>(maxOffset);

    m_logScrollOffset = static_cast<size_t>(next);
    m_logAutoScroll = (m_logScrollOffset == maxOffset);
}

size_t DashboardModel::LogScrollOffset() const
{
    const size_t maxOffset = MaxScrollOffset(m_log.Size(), m_logViewportRows);
    return m_logScrollOffset < maxOffset ? m_logScrollOffset : maxOffset;
}

RowRange DashboardModel::VisibleLogRows()
{
    const size_t total = m_log.Size();
    const size_t maxOffset = MaxScrollOffset(total, m_logViewportRows);

    if (m_logAutoScroll)
        m_logScrollOffset = maxOffset;
    else if (m_logScrollOffset > maxOffset)
        m_logScrollOffset = maxOffset;

    return ComputeVisibleRange(total, m_logViewportRows, m_logScrollOffset);
}

std::string DashboardModel::SubmitCommand(std::string_view aLine)
{
    const std::string line = Trim(aLine);
    if (line.empty())
        return std::string();

    if (m_history.empty() || m_history.back() != line)
    {
        m_history.push_back(line);
        if (m_history.size() > kMaxHistory)
            m_history.erase(m_history.begin());
    }
    m_historyCursor = m_history.size();

    EchoLine(LogLevel::Info, "> " + line);
    return line;
}

std::string DashboardModel::HistoryUp()
{
    if (m_history.empty())
        return std::string();

    if (m_historyCursor > m_history.size())
        m_historyCursor = m_history.size();
    if (m_historyCursor == 0)
        return m_history.front();

    --m_historyCursor;
    return m_history[m_historyCursor];
}

std::string DashboardModel::HistoryDown()
{
    if (m_history.empty())
        return std::string();

    if (m_historyCursor >= m_history.size())
    {
        m_historyCursor = m_history.size();
        return std::string(); // past the newest entry: the line being typed
    }

    ++m_historyCursor;
    if (m_historyCursor >= m_history.size())
    {
        m_historyCursor = m_history.size();
        return std::string();
    }
    return m_history[m_historyCursor];
}

void DashboardModel::EchoLine(LogLevel aLevel, std::string_view aText)
{
    LogRecord record;
    record.seq = 0; // not a ring ticket: this record was never pushed through LogRing
    record.level = aLevel;
    record.timestampMs = m_nowMs;
    record.threadId = 0;
    record.len = static_cast<uint32_t>(aText.size());
    record.text = aText.data();
    m_log.Append(record);
}

} // namespace serverui
