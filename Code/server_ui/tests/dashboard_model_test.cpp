// dashboard_model_test.cpp - acceptance tests for serverui::DashboardModel.
// Plain CHECK macro, no gtest. Returns non-zero on the first failure.
//
// DashboardModel is the whole reason the GUI stream is verifiable on a host
// without imgui.h: everything the dashboard decides is asserted here, and
// DashboardImGui.cpp is left with nothing to decide.

#include "../src/ui/DashboardModel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace serverui;

namespace
{
#define CHECK(cond)                                                                        \
    do                                                                                     \
    {                                                                                      \
        if (!(cond))                                                                       \
        {                                                                                  \
            std::fprintf(stderr, "CHECK FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                  \
        }                                                                                  \
    } while (false)

LogRecord MakeRecord(LogLevel aLevel, const std::string& acText)
{
    LogRecord rec;
    rec.seq = 0;
    rec.level = aLevel;
    rec.timestampMs = 0;
    rec.threadId = 1;
    rec.len = static_cast<uint32_t>(acText.size());
    rec.text = acText.c_str();
    return rec;
}

// Independent reference implementation of the visible window, used to prove
// the model's version over a wide parameter sweep.
RowRange ReferenceRange(size_t aTotal, size_t aViewport, size_t aOffset)
{
    if (aTotal == 0 || aViewport == 0)
        return RowRange{0, 0};

    const size_t viewport = aViewport < aTotal ? aViewport : aTotal;
    size_t offset = aOffset;
    const size_t maxOffset = aTotal - viewport;
    if (offset > maxOffset)
        offset = maxOffset;
    return RowRange{offset, offset + viewport};
}

bool SameRange(const RowRange& a, const RowRange& b)
{
    return a.first == b.first && a.last == b.last;
}

// ---------------------------------------------------------------------------
// (a) virtual range math: exact for many (total, viewport, offset) combos,
//     clamped at both ends, and empty for degenerate inputs.
// ---------------------------------------------------------------------------
void TestVirtualRange()
{
    // Degenerate inputs.
    CHECK(SameRange(ComputeVisibleRange(0, 0, 0), RowRange{0, 0}));
    CHECK(SameRange(ComputeVisibleRange(0, 10, 0), RowRange{0, 0}));
    CHECK(SameRange(ComputeVisibleRange(0, 10, 5), RowRange{0, 0}));
    CHECK(SameRange(ComputeVisibleRange(10, 0, 0), RowRange{0, 0}));
    CHECK(SameRange(ComputeVisibleRange(10, 0, 3), RowRange{0, 0}));

    // Content shorter than the viewport: everything visible, no scrolling.
    CHECK(SameRange(ComputeVisibleRange(3, 10, 0), RowRange{0, 3}));
    CHECK(SameRange(ComputeVisibleRange(3, 10, 99), RowRange{0, 3}));
    CHECK(MaxScrollOffset(3, 10) == 0);
    CHECK(MaxScrollOffset(0, 10) == 0);
    CHECK(MaxScrollOffset(10, 0) == 0);

    // Exactly full: still no scrolling.
    CHECK(SameRange(ComputeVisibleRange(10, 10, 0), RowRange{0, 10}));
    CHECK(MaxScrollOffset(10, 10) == 0);

    // One row over: exactly one row of scroll available.
    CHECK(MaxScrollOffset(11, 10) == 1);
    CHECK(SameRange(ComputeVisibleRange(11, 10, 0), RowRange{0, 10}));
    CHECK(SameRange(ComputeVisibleRange(11, 10, 1), RowRange{1, 11}));
    CHECK(SameRange(ComputeVisibleRange(11, 10, 2), RowRange{1, 11})); // clamped at the bottom

    // Pinned to the bottom.
    CHECK(SameRange(ComputeVisibleRange(1000, 20, 980), RowRange{980, 1000}));
    CHECK(SameRange(ComputeVisibleRange(1000, 20, 100000), RowRange{980, 1000}));

    // Exhaustive sweep against the reference, with offsets deliberately
    // running past both ends.
    size_t combos = 0;
    for (size_t total = 0; total <= 70; ++total)
    {
        for (size_t viewport = 0; viewport <= 25; ++viewport)
        {
            const size_t maxOffset = MaxScrollOffset(total, viewport);
            const size_t offsets[] = {0, 1, 2, maxOffset > 1 ? maxOffset - 1 : 0, maxOffset,
                maxOffset + 1, maxOffset + 7, 1000};
            for (size_t offset : offsets)
            {
                const RowRange got = ComputeVisibleRange(total, viewport, offset);
                CHECK(SameRange(got, ReferenceRange(total, viewport, offset)));

                // Invariants that must hold for every window.
                if (got.Empty())
                {
                    CHECK(total == 0 || viewport == 0);
                }
                else
                {
                    CHECK(got.last <= total);
                    CHECK(got.Count() == (viewport < total ? viewport : total));
                    CHECK(got.first + got.Count() == got.last);
                }
                ++combos;
            }
        }
    }
    CHECK(combos > 10000);

    std::printf("[a] virtual range exact over %zu (total, viewport, offset) combos\n", combos);
}

// ---------------------------------------------------------------------------
// (b) auto-scroll: pinned to the bottom while enabled, disabled the moment the
//     user scrolls up, re-armed when they scroll back to the bottom.
// ---------------------------------------------------------------------------
void TestAutoScroll()
{
    LogModel log(64);
    DashboardModel model(log);
    model.SetLogViewportRows(5);

    for (int i = 0; i < 20; ++i)
    {
        const std::string text = "line " + std::to_string(i);
        const LogRecord rec = MakeRecord(LogLevel::Info, text);
        log.Append(rec);
    }
    CHECK(model.LogRowCount() == 20);

    // Auto-scroll starts enabled: the first frame pins to the bottom.
    CHECK(model.LogAutoScroll());
    RowRange range = model.VisibleLogRows();
    CHECK(SameRange(range, RowRange{15, 20}));
    CHECK(model.LogScrollOffset() == 15);

    // New rows arriving keep the view pinned, with no help from the caller.
    for (int i = 20; i < 30; ++i)
    {
        const std::string text = "line " + std::to_string(i);
        const LogRecord rec = MakeRecord(LogLevel::Info, text);
        log.Append(rec);
    }
    range = model.VisibleLogRows();
    CHECK(SameRange(range, RowRange{25, 30}));

    // Scrolling up disables auto-scroll and stays where it was put.
    model.ScrollLogBy(-3);
    CHECK(!model.LogAutoScroll());
    CHECK(model.LogScrollOffset() == 22);
    range = model.VisibleLogRows();
    CHECK(SameRange(range, RowRange{22, 27}));

    // New rows must NOT yank the view back to the bottom while it is off.
    for (int i = 30; i < 40; ++i)
    {
        const std::string text = "line " + std::to_string(i);
        const LogRecord rec = MakeRecord(LogLevel::Info, text);
        log.Append(rec);
    }
    range = model.VisibleLogRows();
    CHECK(SameRange(range, RowRange{22, 27}));
    CHECK(!model.LogAutoScroll());

    // Scrolling back down to the bottom re-enables it.
    model.ScrollLogBy(1000);
    CHECK(model.LogAutoScroll());
    CHECK(SameRange(model.VisibleLogRows(), RowRange{35, 40}));

    // One row short of the bottom is still "off".
    model.ScrollLogTo(33);
    CHECK(!model.LogAutoScroll());
    CHECK(model.LogScrollOffset() == 33);

    // Explicitly toggling it on snaps to the bottom immediately.
    model.SetLogAutoScroll(true);
    CHECK(model.LogAutoScroll());
    CHECK(model.LogScrollOffset() == 35);
    CHECK(SameRange(model.VisibleLogRows(), RowRange{35, 40}));

    // Toggling off leaves the offset untouched.
    model.SetLogAutoScroll(false);
    CHECK(!model.LogAutoScroll());
    CHECK(model.LogScrollOffset() == 35);

    // Overscrolling up clamps at zero and stays off.
    model.ScrollLogBy(-1000);
    CHECK(model.LogScrollOffset() == 0);
    CHECK(!model.LogAutoScroll());
    CHECK(SameRange(model.VisibleLogRows(), RowRange{0, 5}));

    // Auto-scroll survives a filter that shrinks the list, and still pins.
    model.SetLogAutoScroll(true);
    model.SetLogViewportRows(3);
    CHECK(SameRange(model.VisibleLogRows(), RowRange{37, 40}));

    // A viewport taller than the content has no scroll to pin to.
    model.SetLogViewportRows(500);
    CHECK(model.LogScrollOffset() == 0);
    CHECK(SameRange(model.VisibleLogRows(), RowRange{0, 40}));

    std::printf("[b] auto-scroll pins, disables on scroll-up, re-arms at the bottom\n");
}

// ---------------------------------------------------------------------------
// (c) an unmeasured ping renders "-", never "0".
// ---------------------------------------------------------------------------
void TestPingUnknown()
{
    CHECK(FormatPing(0) == "-");
    CHECK(FormatPing(1) == "1");
    CHECK(FormatPing(37) == "37");
    CHECK(FormatPing(9999) == "9999");

    LogModel log(8);
    DashboardModel model(log);

    std::vector<PlayerRow> rows;
    PlayerRow known;
    known.connectionId = 7;
    known.playerId = 70;
    known.username = "Alice";
    known.pingMs = 42;
    known.connectedAtMs = 1000;
    rows.push_back(known);

    PlayerRow unknown;
    unknown.connectionId = 8;
    unknown.playerId = 80;
    unknown.username = "Bob";
    unknown.pingMs = 0; // not measured yet
    unknown.connectedAtMs = 2000;
    rows.push_back(unknown);

    PlayerRow unnamed;
    unnamed.connectionId = 9;
    unnamed.playerId = 90;
    unnamed.username = nullptr;
    unnamed.pingMs = 0;
    unnamed.connectedAtMs = 5000;
    unnamed.isAdmin = true;
    rows.push_back(unnamed);

    model.Update(CoreSnapshot{}, 11000);
    model.SetPlayers(rows);
    CHECK(model.PlayerCount() == 3);

    const PlayerCell cell0 = model.PlayerCellAt(0);
    CHECK(cell0.ping == "42");
    CHECK(cell0.id == "7");
    CHECK(cell0.username == "Alice");
    CHECK(cell0.role == "player");
    CHECK(cell0.duration == "10s"); // 11000 - 1000

    const PlayerCell cell1 = model.PlayerCellAt(1);
    CHECK(cell1.ping == "-");
    CHECK(cell1.ping != "0");
    CHECK(cell1.duration == "9s");

    const PlayerCell cell2 = model.PlayerCellAt(2);
    CHECK(cell2.ping == "-");
    CHECK(cell2.username == "-");
    CHECK(cell2.role == "admin");
    CHECK(cell2.isAdmin);
    CHECK(cell2.duration == "6s");

    // A clock that went backwards must not produce a negative duration.
    model.Update(CoreSnapshot{}, 100);
    CHECK(model.PlayerCellAt(0).duration == "0s");

    // Out of range is a visible "-" row, not a crash.
    const PlayerCell bogus = model.PlayerCellAt(99);
    CHECK(bogus.ping == "-");
    CHECK(bogus.username == "-");
    CHECK(bogus.id == "-");

    std::printf("[c] unknown ping renders \"-\", never \"0\"\n");
}

// ---------------------------------------------------------------------------
// (d) uptime formatting at the boundaries.
// ---------------------------------------------------------------------------
void TestUptimeFormatting()
{
    CHECK(FormatUptime(0) == "0s");
    CHECK(FormatUptime(1) == "0s");      // sub-second truncates
    CHECK(FormatUptime(999) == "0s");
    CHECK(FormatUptime(1000) == "1s");
    CHECK(FormatUptime(59000) == "59s");
    CHECK(FormatUptime(59999) == "59s");
    CHECK(FormatUptime(60000) == "1m 00s");
    CHECK(FormatUptime(61000) == "1m 01s");
    CHECK(FormatUptime(3599000) == "59m 59s");
    CHECK(FormatUptime(3600000) == "1h 00m 00s");
    CHECK(FormatUptime(3661000) == "1h 01m 01s");
    CHECK(FormatUptime(86399000) == "23h 59m 59s");
    CHECK(FormatUptime(86400000) == "1d 00h 00m 00s");
    CHECK(FormatUptime(90061000) == "1d 01h 01m 01s");
    CHECK(FormatUptime(7ull * 86400000ull) == "7d 00h 00m 00s");
    CHECK(FormatUptime(7ull * 86400000ull + 3661000ull) == "7d 01h 01m 01s");
    CHECK(FormatUptime(30ull * 86400000ull) == "30d 00h 00m 00s");

    // The status bar reads the same formatter off the snapshot.
    LogModel log(8);
    DashboardModel model(log);
    CoreSnapshot snap;
    snap.uptimeMs = 3661000;
    model.Update(snap, 0);
    CHECK(model.UptimeText() == "1h 01m 01s");

    std::printf("[d] uptime formatting exact at 0s/59s/60s/1h/24h+/7d+\n");
}

// ---------------------------------------------------------------------------
// (e) command history up/down with bounds.
// ---------------------------------------------------------------------------
void TestCommandHistory()
{
    LogModel log(32);
    DashboardModel model(log);

    // Empty history: navigation is a no-op and yields an empty line.
    CHECK(model.HistorySize() == 0);
    CHECK(model.HistoryUp().empty());
    CHECK(model.HistoryDown().empty());

    CHECK(model.SubmitCommand("status") == "status");
    CHECK(model.SubmitCommand("  players  ") == "players"); // trimmed
    CHECK(model.SubmitCommand("kick 3") == "kick 3");
    CHECK(model.HistorySize() == 3);

    // Blank submissions are rejected and do not enter the history.
    CHECK(model.SubmitCommand("").empty());
    CHECK(model.SubmitCommand("   ").empty());
    CHECK(model.SubmitCommand("\t\r\n").empty());
    CHECK(model.HistorySize() == 3);

    // Up walks backwards from the newest.
    CHECK(model.HistoryUp() == "kick 3");
    CHECK(model.HistoryUp() == "players");
    CHECK(model.HistoryUp() == "status");
    // Clamped at the oldest: repeated Up stays there.
    CHECK(model.HistoryUp() == "status");
    CHECK(model.HistoryUp() == "status");

    // Down walks forwards and ends on an empty line (the one being typed).
    CHECK(model.HistoryDown() == "players");
    CHECK(model.HistoryDown() == "kick 3");
    CHECK(model.HistoryDown().empty());
    // Clamped past the newest.
    CHECK(model.HistoryDown().empty());
    CHECK(model.HistoryDown().empty());

    // Up after falling off the end restarts from the newest.
    CHECK(model.HistoryUp() == "kick 3");

    // An immediate repeat is not stored twice, but does reset the cursor.
    CHECK(model.SubmitCommand("kick 3") == "kick 3");
    CHECK(model.HistorySize() == 3);
    CHECK(model.HistoryUp() == "kick 3");

    // Non-immediate repeats are stored.
    CHECK(model.SubmitCommand("status") == "status");
    CHECK(model.HistorySize() == 4);
    CHECK(model.HistoryUp() == "status");
    CHECK(model.HistoryUp() == "kick 3");

    // Submissions are echoed into the log view.
    CHECK(log.Size() > 0);
    const LogRecord* last = log.Get(log.Size() - 1);
    CHECK(last != nullptr);
    CHECK(std::strcmp(last->text, "> status") == 0);

    // History is bounded.
    for (int i = 0; i < 400; ++i)
        model.SubmitCommand("cmd " + std::to_string(i));
    CHECK(model.HistorySize() == DashboardModel::kMaxHistory);
    CHECK(model.HistoryAt(model.HistorySize() - 1) == "cmd 399");
    CHECK(model.HistoryAt(0) == "cmd 144"); // 400 - 256

    std::printf("[e] command history up/down exact, bounded at %zu\n", DashboardModel::kMaxHistory);
}

// ---------------------------------------------------------------------------
// Supporting decisions the ImGui layer must not have to make itself.
// ---------------------------------------------------------------------------
void TestStateAndMetrics()
{
    LogModel log(8);
    DashboardModel model(log);

    CoreSnapshot snap;
    snap.state = RunState::Stopped;
    model.Update(snap, 0);
    CHECK(std::strcmp(model.StateLabel(), "Stopped") == 0);
    CHECK(model.StateColorOf() == StateColor::Gray);

    snap.state = RunState::Starting;
    model.Update(snap, 0);
    CHECK(std::strcmp(model.StateLabel(), "Starting") == 0);
    CHECK(model.StateColorOf() == StateColor::Amber);

    snap.state = RunState::Running;
    snap.port = 10578;
    snap.playerCount = 4;
    snap.tps = 59.94;
    snap.frameTimeMs = 16.7;
    snap.cpuPercent = 12.5;
    snap.memRssBytes = 512ull * 1024ull * 1024ull;
    snap.netInBytesPerSec = 2048;
    snap.netOutBytesPerSec = 0;
    snap.packetsIn = 1200;
    snap.packetsOut = 800;
    model.Update(snap, 1000);
    CHECK(model.StateColorOf() == StateColor::Green);
    CHECK(model.PortText() == "10578");
    CHECK(model.PlayerCountText() == "4");
    CHECK(model.TpsText() == "59.9");
    CHECK(model.FrameTimeText() == "16.7 ms");
    CHECK(model.CpuText() == "12.5%");
    CHECK(model.MemText() == "512.0 MB");
    CHECK(model.NetInText() == "2.0 KB/s");
    CHECK(model.NetOutText() == "-"); // 0 == not measured
    CHECK(model.PacketsInText() == "1200");
    CHECK(model.PacketsOutText() == "800");

    snap.state = RunState::Stopping;
    model.Update(snap, 1000);
    CHECK(std::strcmp(model.StateLabel(), "Stopping") == 0);
    CHECK(model.StateColorOf() == StateColor::Red);

    // Unmeasured metrics are "-", never a fabricated 0.
    model.Update(CoreSnapshot{}, 0);
    CHECK(model.TpsText() == "-");
    CHECK(model.FrameTimeText() == "-");
    CHECK(model.CpuText() == "-");
    CHECK(model.MemText() == "-");
    CHECK(model.NetInText() == "-");
    CHECK(model.PacketsInText() == "-");
    CHECK(model.PortText() == "-");

    // Packet rate is a delta between frames, so it needs a fresh model: the
    // one above has already been primed by earlier Update calls.
    LogModel rateLog(8);
    DashboardModel rate(rateLog);
    CHECK(rate.PacketsPerSecText() == "-"); // nothing measured at all yet

    CoreSnapshot a;
    a.packetsIn = 100;
    a.packetsOut = 100;
    rate.Update(a, 1000);
    CHECK(rate.PacketsPerSec() == 0.0); // first frame has no baseline

    CoreSnapshot b;
    b.packetsIn = 150;
    b.packetsOut = 150;
    rate.Update(b, 2000);
    CHECK(rate.PacketsPerSec() == 100.0); // 100 packets over 1 s
    CHECK(rate.PacketsPerSecText() == "100/s");

    // Half a second between frames doubles the rate for the same delta.
    CoreSnapshot c;
    c.packetsIn = 200;
    c.packetsOut = 200;
    rate.Update(c, 2500);
    CHECK(rate.PacketsPerSec() == 200.0);

    // A counter reset must not produce a negative rate.
    CoreSnapshot d;
    d.packetsIn = 5;
    d.packetsOut = 5;
    rate.Update(d, 3500);
    CHECK(rate.PacketsPerSec() == 0.0);

    // A clock that did not advance cannot yield a division by zero.
    CoreSnapshot e;
    e.packetsIn = 9;
    e.packetsOut = 9;
    rate.Update(e, 3500);
    CHECK(rate.PacketsPerSec() == 0.0);

    std::printf("[x] state labels/colours and metric formatting exact\n");
}

void TestLogColoursAndFilters()
{
    CHECK(DashboardModel::LogColorOf(LogLevel::Trace) == LogColor::Muted);
    CHECK(DashboardModel::LogColorOf(LogLevel::Debug) == LogColor::Subtle);
    CHECK(DashboardModel::LogColorOf(LogLevel::Info) == LogColor::Normal);
    CHECK(DashboardModel::LogColorOf(LogLevel::Warn) == LogColor::Warning);
    CHECK(DashboardModel::LogColorOf(LogLevel::Error) == LogColor::Danger);
    CHECK(DashboardModel::LogColorOf(LogLevel::Critical) == LogColor::Fatal);

    LogModel log(64);
    DashboardModel model(log);
    model.SetLogViewportRows(10);

    const LogLevel levels[] = {LogLevel::Info, LogLevel::Warn, LogLevel::Error, LogLevel::Debug};
    for (int i = 0; i < 40; ++i)
    {
        const std::string text = "event " + std::to_string(i) + (i % 4 == 1 ? " BOB" : "");
        const LogRecord rec = MakeRecord(levels[i % 4], text);
        log.Append(rec);
    }
    CHECK(model.LogRowCount() == 40);

    // Level filter drives LogModel and the row count.
    model.SetLogMinLevel(LogLevel::Error);
    CHECK(model.LogRowCount() == 10); // 40 / 4
    CHECK(model.VisibleLogRows().last <= model.LogRowCount());

    model.SetLogMinLevel(LogLevel::Trace);
    CHECK(model.LogRowCount() == 40);

    // Keyword filter, case-insensitive then sensitive.
    model.SetLogKeyword("bob");
    CHECK(model.LogRowCount() == 10);
    model.SetLogCaseSensitive(true);
    CHECK(model.LogRowCount() == 0);
    model.SetLogKeyword("BOB");
    CHECK(model.LogRowCount() == 10);
    CHECK(model.LogFilterState().caseSensitive);
    CHECK(model.LogFilterState().substring == "BOB");

    model.SetLogKeyword("");
    CHECK(model.LogRowCount() == 40);

    // LogRowAt maps a visible index to the record, and is safe out of range.
    const LogRecord* first = model.LogRowAt(0);
    CHECK(first != nullptr);
    CHECK(std::strcmp(first->text, "event 0") == 0);
    CHECK(model.LogRowAt(40) == nullptr);

    std::printf("[x] log colour mapping and filter plumbing exact\n");
}

void TestSparklineWiring()
{
    LogModel log(8);
    DashboardModel model(log, 4);

    for (int i = 1; i <= 6; ++i)
    {
        CoreSnapshot snap;
        snap.tps = 50.0 + i;
        snap.frameTimeMs = 10.0 + i;
        model.Update(snap, static_cast<uint64_t>(i) * 1000);
    }

    const Sparkline& tps = model.TpsHistory();
    CHECK(tps.Capacity() == 4);
    CHECK(tps.Size() == 4);
    CHECK(tps.At(0) == 53.0); // 51,52 evicted
    CHECK(tps.At(3) == 56.0);
    CHECK(tps.Min() == 53.0);
    CHECK(tps.Max() == 56.0);

    const Sparkline& frame = model.FrameTimeHistory();
    CHECK(frame.Size() == 4);
    CHECK(frame.Last() == 16.0);

    std::printf("[x] sparkline histories fed from the snapshot\n");
}

} // namespace

int main()
{
    TestVirtualRange();
    TestAutoScroll();
    TestPingUnknown();
    TestUptimeFormatting();
    TestCommandHistory();
    TestStateAndMetrics();
    TestLogColoursAndFilters();
    TestSparklineWiring();
    std::printf("ALL DASHBOARD MODEL TESTS PASSED\n");
    return 0;
}
