#pragma once

// DashboardModel: every decision and every formatted string the dashboard
// draws, expressed with zero UI-framework types.
//
// The ImGui layer (DashboardImGui.cpp) is a mechanical translation of what this
// class reports into widgets and holds no logic of its own. That split is what
// makes the dashboard verifiable on a host where imgui.h is unavailable.
//
// The model owns no thread, no timer and no clock: the caller passes the frame
// timestamp into Update(), so every formatted string here is deterministic and
// can be asserted in a plain test binary.

#include "Sparkline.h"

#include "../core/ServerCore.h"
#include "../log/LogModel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace serverui
{

// Colour *class*, not a colour. The ImGui layer maps these to ImVec4, so this
// header never mentions a renderer type.
enum class StateColor
{
    Gray,  // Stopped
    Amber, // Starting
    Green, // Running
    Red    // Stopping - winding down, no longer accepting joins
};

enum class LogColor
{
    Muted,   // Trace
    Subtle,  // Debug
    Normal,  // Info
    Warning, // Warn
    Danger,  // Error
    Fatal    // Critical
};

// Half-open row window [first, last): the only rows a virtualised list may
// touch during a frame.
struct RowRange
{
    size_t first{0};
    size_t last{0};

    size_t Count() const { return last > first ? last - first : 0; }
    bool Empty() const { return last <= first; }
};

// Largest usable scroll offset. 0 when the content cannot scroll at all:
// an empty list, or a viewport at least as tall as the content.
size_t MaxScrollOffset(size_t aTotalRows, size_t aViewportRows) noexcept;

// Clamps aScrollOffset into [0, MaxScrollOffset] and returns the visible
// window. aTotalRows == 0 or aViewportRows == 0 yields {0, 0}.
RowRange ComputeVisibleRange(size_t aTotalRows, size_t aViewportRows, size_t aScrollOffset) noexcept;

// "0s", "59s", "1m 00s", "1h 00m 00s", "1d 00h 00m 00s", "7d 00h 00m 00s".
// Sub-second input truncates to whole seconds.
std::string FormatUptime(uint64_t aMs);

// "42"; "-" when the core has not measured the ping yet (PlayerRow::pingMs == 0
// means unknown, not zero milliseconds).
std::string FormatPing(uint32_t aPingMs);

// The four helpers below render "-" for 0, which the CoreSnapshot contract
// defines as "not measured yet". They never fabricate a reading.
std::string FormatPercentOrDash(double aPercent);
std::string FormatBytesOrDash(uint64_t aBytes);
std::string FormatRateOrDash(uint64_t aBytesPerSec);
std::string FormatCountOrDash(uint64_t aCount);

// One row of the player table, already formatted.
struct PlayerCell
{
    std::string id;       // connection id, as the ID column shows it
    std::string username; // "-" when the core has no name for the slot yet
    std::string ping;     // "-" when not measured
    std::string duration; // connected time, e.g. "1m 03s"
    std::string role;     // "admin" / "player"
    uint32_t playerId{0};
    bool isAdmin{false};
};

class DashboardModel
{
public:
    static constexpr size_t kMaxHistory = 256;

    explicit DashboardModel(LogModel& aLog, size_t aSparkCapacity = 120);

    DashboardModel(const DashboardModel&) = delete;
    DashboardModel& operator=(const DashboardModel&) = delete;

    // ----------------------------------------------------------------- frame
    // aNowMs is the caller's frame timestamp; it drives the connected-duration
    // cells and the packet-rate delta, and nothing else.
    void Update(const CoreSnapshot& acSnapshot, uint64_t aNowMs);
    void SetPlayers(const std::vector<PlayerRow>& acPlayers);

    const CoreSnapshot& Snapshot() const { return m_snapshot; }
    uint64_t NowMs() const { return m_nowMs; }

    // ------------------------------------------------------------ status bar
    std::string UptimeText() const;
    const char* StateLabel() const;
    StateColor StateColorOf() const;
    std::string PortText() const;
    std::string PlayerCountText() const;

    // --------------------------------------------------------------- metrics
    std::string TpsText() const;
    std::string FrameTimeText() const;
    std::string CpuText() const;
    std::string MemText() const;
    std::string NetInText() const;
    std::string NetOutText() const;
    std::string PacketsInText() const;
    std::string PacketsOutText() const;
    std::string PacketsPerSecText() const;

    double PacketsPerSec() const { return m_packetsPerSec; }

    const Sparkline& TpsHistory() const { return m_tpsHistory; }
    const Sparkline& FrameTimeHistory() const { return m_frameTimeHistory; }

    // --------------------------------------------------------------- players
    size_t PlayerCount() const { return m_players.size(); }
    // Out-of-range returns an all "-" cell so a caller bug renders visibly.
    PlayerCell PlayerCellAt(size_t aIndex) const;

    // ------------------------------------------------------------- log view
    size_t LogRowCount() const { return m_log.Size(); }
    size_t LogStoredCount() const { return m_log.StoredCount(); }

    // Valid only until the next LogRowAt call: LogModel::Get hands back one
    // shared scratch record. Read it and draw it in the same statement.
    const LogRecord* LogRowAt(size_t aVisibleIndex) const { return m_log.Get(aVisibleIndex); }

    static LogColor LogColorOf(LogLevel aLevel) noexcept;

    const LogFilter& LogFilterState() const { return m_filter; }
    void SetLogKeyword(std::string_view aKeyword);
    void SetLogMinLevel(LogLevel aLevel);
    void SetLogCaseSensitive(bool aCaseSensitive);

    // Rows the viewport can show. Drives the virtual range below.
    void SetLogViewportRows(size_t aRows);
    size_t LogViewportRows() const { return m_logViewportRows; }

    bool LogAutoScroll() const { return m_logAutoScroll; }
    // Enabling pins the offset to the bottom immediately; disabling leaves the
    // offset where it is.
    void SetLogAutoScroll(bool aEnabled);

    // User scroll events. Both re-derive the auto-scroll flag from where they
    // land: not at the bottom disables it, back at the bottom re-enables it.
    void ScrollLogTo(size_t aOffset);
    void ScrollLogBy(long long aDeltaRows);

    size_t LogScrollOffset() const;

    // Call once per frame, before reading the range: re-pins the offset while
    // auto-scroll is on, clamps it otherwise, then returns [first, last).
    RowRange VisibleLogRows();

    bool ExportLog(const std::string& acPath, bool aOnlyVisible) const { return m_log.Export(acPath, aOnlyVisible); }

    // ------------------------------------------------------------- console
    // Trims the line, records it in the history (skipping an immediate repeat)
    // and echoes it into the log view. Returns the trimmed line, or "" when
    // there was nothing to submit - the caller forwards a non-empty result to
    // ServerCore::SubmitCommand.
    std::string SubmitCommand(std::string_view aLine);

    std::string HistoryUp();
    std::string HistoryDown();
    size_t HistorySize() const { return m_history.size(); }
    const std::string& HistoryAt(size_t aIndex) const { return m_history[aIndex]; }

    void EchoLine(LogLevel aLevel, std::string_view aText);

private:
    LogModel& m_log;

    CoreSnapshot m_snapshot;
    std::vector<PlayerRow> m_players;
    uint64_t m_nowMs{0};

    LogFilter m_filter;
    size_t m_logViewportRows{20};
    size_t m_logScrollOffset{0};
    bool m_logAutoScroll{true};

    Sparkline m_tpsHistory;
    Sparkline m_frameTimeHistory;

    uint64_t m_prevPackets{0};
    uint64_t m_prevPacketsMs{0};
    bool m_havePrevPackets{false};
    double m_packetsPerSec{0.0};

    std::vector<std::string> m_history;
    size_t m_historyCursor{0}; // == m_history.size() means "not navigating"
};

} // namespace serverui
