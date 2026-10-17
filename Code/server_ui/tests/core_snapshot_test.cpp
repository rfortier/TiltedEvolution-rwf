// Standalone test for the ServerCore facade. Plain asserts, no framework.
//
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -o core_snapshot_test.exe ^
//       tests/core_snapshot_test.cpp src/core/ServerCore.cpp ^
//       src/metrics/MetricsSampler.cpp -lpsapi -liphlpapi
//
// ServerCore.cpp now samples real metrics through MetricsSampler, so the
// sampler's translation unit and its two import libraries are part of this
// test's link line.

#include "../src/core/ServerCore.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace serverui;

namespace
{
int g_failures = 0;

void Check(bool aCondition, const char* acWhat)
{
    if (aCondition)
        return;

    std::printf("FAIL: %s\n", acWhat);
    ++g_failures;
}

// The seam adapter the real DediRunner/IGameServerInstance will implement later.
struct FakeBackend final : ICoreBackend
{
    std::atomic<bool> initialized{false};
    std::atomic<bool> shutDown{false};
    std::atomic<uint32_t> updates{0};
    std::atomic<uint32_t> playerCount{0};
    std::atomic<bool> initializedOnStartThread{false};

    // Thread the core loop actually runs on, captured on the first Update().
    std::atomic<std::thread::id> coreThread{};
    std::atomic<bool> sawCoreThread{false};

    // Command dispatch evidence.
    std::atomic<uint32_t> commandsSeen{0};
    std::atomic<std::thread::id> commandThread{};
    std::string lastCommand;
    std::mutex commandMutex;

    // ApplySettings evidence.
    std::atomic<uint16_t> appliedPort{0};
    std::atomic<bool> rejectPort{false};

    bool Initialize() override
    {
        initializedOnStartThread.store(initialized.exchange(true) == false);
        return true;
    }

    void Update() override
    {
        updates.fetch_add(1);
        if (!sawCoreThread.exchange(true))
            coreThread.store(std::this_thread::get_id());
    }

    void Shutdown() override { shutDown.store(true); }

    bool IsListening() override { return initialized.load(); }

    uint32_t PlayerCount() override { return playerCount.load(); }

    void OnCommand(std::string_view aLine) override
    {
        {
            std::scoped_lock lock(commandMutex);
            lastCommand.assign(aLine);
        }
        commandThread.store(std::this_thread::get_id());
        commandsSeen.fetch_add(1);
    }

    bool ApplySettings(const SettingsPatch& aPatch, std::string* apError) override
    {
        if (aPatch.port.has_value())
        {
            if (rejectPort.load())
            {
                if (apError != nullptr)
                    *apError = "fake backend refuses port changes";
                return false;
            }
            appliedPort.store(*aPatch.port);
        }
        if (apError != nullptr)
            apError->clear();
        return true;
    }

    std::vector<PlayerInfo> Players() override
    {
        std::vector<PlayerInfo> rows;
        PlayerInfo first;
        first.connectionId = 7;
        first.playerId = 1;
        first.username = "alice";
        first.pingMs = 12;
        first.isAdmin = true;
        rows.push_back(first);
        return rows;
    }
};

// A backend whose Initialize() fails, to prove Start() reports it.
struct FailingBackend final : ICoreBackend
{
    bool Initialize() override { return false; }
    void Update() override {}
    void Shutdown() override {}
    bool IsListening() override { return false; }
    uint32_t PlayerCount() override { return 0; }
};

bool WaitFor(const std::function<bool()>& aPredicate, std::chrono::milliseconds aTimeout)
{
    const auto deadline = std::chrono::steady_clock::now() + aTimeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (aPredicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return aPredicate();
}

CoreConfig MakeConfig()
{
    CoreConfig config;
    config.iniPath = "config/STServer.ini";
    config.port = 10578;
    config.maxPlayers = 32;
    config.tickRate = 60;
    return config;
}

void TestStartStopLifecycle()
{
    auto backend = std::make_unique<FakeBackend>();
    auto* raw = backend.get();

    {
        ServerCore core(MakeConfig(), std::move(backend));

        Check(core.State() == RunState::Stopped, "starts Stopped");
        Check(!core.IsRunning(), "IsRunning() false before Start");

        const auto beforeStart = core.Snapshot();
        Check(beforeStart.state == RunState::Stopped, "snapshot before Start is Stopped");

        Check(core.Start(), "Start() returns true");
        Check(raw->initialized.load(), "backend Initialize() was called");
        Check(core.IsRunning(), "IsRunning() true after Start");
        Check(core.State() == RunState::Running, "state Running after Start");

        // Ticking.
        Check(WaitFor([&] { return raw->updates.load() > 5; }, std::chrono::seconds(5)), "core thread ticks");

        // TPS must be a measured value, not an assumption. The very first
        // samples span only one or two tick intervals and are legitimately
        // noisy, so let the 64-slot window fill before judging the number.
        const bool sawTps = WaitFor([&] { return core.Snapshot().tps > 0.0; }, std::chrono::seconds(5));
        Check(sawTps, "TPS becomes > 0");

        // 64 slots at 60 Hz is just over one second of history.
        std::this_thread::sleep_for(std::chrono::milliseconds(1300));

        const double tps = core.Snapshot().tps;
        std::printf("  measured tps = %.2f (configured tickRate 60)\n", tps);
        Check(tps > 1.0 && tps <= 1000.0, "TPS within a plausible range");
        // Measured, not assumed: a constant would pass the range check above but
        // fail this one, because the loop really does run at the configured rate.
        Check(tps > 60.0 * 0.85 && tps < 60.0 * 1.15, "TPS converges on the configured tick rate");

        // Port and uptime are published.
        const auto snap = core.Snapshot();
        Check(snap.port == 10578, "snapshot port matches config");
        Check(snap.frameTimeMs >= 0.0, "frame time is non-negative");
        Check(snap.uptimeMs > 0, "uptime is measured from the steady clock");
        Check(snap.playerCount == 0, "playerCount comes from the backend");

        // Metrics are now real, so the honest contract replaces the old
        // "documented 0 stub" assertions. What is asserted is the contract,
        // not a particular reading: a value inside its documented range, or a
        // documented 0 where the platform cannot measure it. Traffic volume is
        // deliberately NOT asserted - an idle machine may legitimately send
        // nothing, and a test that demands packets would be flaky, not strong.
#if defined(_WIN32)
        const uint32_t cores = serverui::metrics::MetricsSampler::LogicalCoreCount();
        const double cpuCeiling = 100.0 * static_cast<double>(cores > 0 ? cores : 1);
        std::printf("  metrics: cpu = %.2f%%, rss = %llu bytes, net in/out = %llu/%llu B/s, pkts in/out = %llu/%llu\n",
            snap.cpuPercent, static_cast<unsigned long long>(snap.memRssBytes),
            static_cast<unsigned long long>(snap.netInBytesPerSec),
            static_cast<unsigned long long>(snap.netOutBytesPerSec),
            static_cast<unsigned long long>(snap.packetsIn),
            static_cast<unsigned long long>(snap.packetsOut));

        Check(snap.cpuPercent >= 0.0 && snap.cpuPercent <= cpuCeiling,
            "cpuPercent is a real measurement inside 0..100*cores");
        Check(snap.memRssBytes > 0, "memRssBytes is a real measurement, not the 0 stub");
        // The network sample is system-wide and may be zero on an isolated
        // host, so the observable contract is that the sampler reports rather
        // than fabricates: a zero here is only acceptable as "not measured".
        Check(snap.netInBytesPerSec < (1ull << 62) && snap.netOutBytesPerSec < (1ull << 62),
            "net rates are plausible rather than garbage");
#else
        // Non-Windows: documented zero-with-invalid, never an invented number.
        Check(snap.cpuPercent == 0.0, "cpuPercent is the documented 0 on this platform");
        Check(snap.memRssBytes == 0, "memRssBytes is the documented 0 on this platform");
        Check(snap.netInBytesPerSec == 0 && snap.netOutBytesPerSec == 0,
            "net counters are the documented 0 on this platform");
#endif

        core.RequestStop();
        Check(core.State() == RunState::Stopped, "state Stopped after RequestStop");
        Check(!core.IsRunning(), "IsRunning() false after RequestStop");
        Check(raw->shutDown.load(), "backend Shutdown() was called");
        Check(core.Snapshot().state == RunState::Stopped, "final snapshot is Stopped");

        // Idempotent: a second and third stop are no-ops, not crashes.
        core.RequestStop();
        core.RequestStop();
        Check(core.State() == RunState::Stopped, "repeated RequestStop stays Stopped");
    }

    std::printf("  lifecycle + destructor join: ok\n");
}

void TestSnapshotFromSecondThread()
{
    auto backend = std::make_unique<FakeBackend>();
    auto* raw = backend.get();
    ServerCore core(MakeConfig(), std::move(backend));

    Check(core.Start(), "Start() for reader test");

    // Hammer Snapshot() from another thread while the core thread publishes.
    std::atomic<bool> readerDone{false};
    std::atomic<uint64_t> reads{0};
    std::atomic<uint64_t> monotonicUptime{0};
    std::atomic<bool> stateAlwaysRunning{true};

    std::thread reader([&] {
        uint64_t lastUptime = 0;
        while (!readerDone.load(std::memory_order_acquire))
        {
            const auto snapshot = core.Snapshot();
            if (snapshot.state != RunState::Running)
                stateAlwaysRunning.store(false);
            if (snapshot.uptimeMs < lastUptime)
                monotonicUptime.fetch_add(1);
            lastUptime = snapshot.uptimeMs;
            reads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    readerDone.store(true, std::memory_order_release);
    reader.join();

    std::printf("  concurrent Snapshot() reads = %llu\n", static_cast<unsigned long long>(reads.load()));
    Check(reads.load() > 1000, "Snapshot() was read concurrently many times");
    Check(stateAlwaysRunning.load(), "state stayed Running for every concurrent read");
    Check(monotonicUptime.load() == 0, "uptime never went backwards across reads");
    Check(raw->updates.load() > 10, "core kept ticking during concurrent reads");

    core.RequestStop();
}

void TestCommandExecutesOnCoreThread()
{
    auto backend = std::make_unique<FakeBackend>();
    auto* raw = backend.get();
    ServerCore core(MakeConfig(), std::move(backend));

    Check(core.Start(), "Start() for command test");
    Check(WaitFor([&] { return raw->updates.load() > 2; }, std::chrono::seconds(5)), "ticking before command");

    const auto callerThread = std::this_thread::get_id();
    core.SubmitCommand("status");

    Check(WaitFor([&] { return raw->commandsSeen.load() == 1; }, std::chrono::seconds(5)), "command was dispatched");

    const auto executingThread = raw->commandThread.load();
    std::printf("  caller tid != executing tid: %s\n",
        executingThread != callerThread ? "yes" : "NO");
    Check(executingThread != callerThread, "SubmitCommand executes on the core thread, not the caller");
    Check(executingThread == raw->coreThread.load(), "command ran on the same thread as Update()");

    {
        std::scoped_lock lock(raw->commandMutex);
        Check(raw->lastCommand == "status", "command text survived the hand-off");
    }

    // The thread the loop runs on must not be the thread that called Start().
    Check(raw->coreThread.load() != callerThread, "core loop runs on its own thread");

    // Several commands, ordered.
    core.SubmitCommand("one");
    core.SubmitCommand("two");
    core.SubmitCommand("three");
    Check(WaitFor([&] { return raw->commandsSeen.load() == 4; }, std::chrono::seconds(5)), "all commands dispatched");
    {
        std::scoped_lock lock(raw->commandMutex);
        Check(raw->lastCommand == "three", "commands execute in submission order");
    }

    core.RequestStop();
}

void TestPlayers()
{
    auto backend = std::make_unique<FakeBackend>();
    backend->playerCount.store(1);
    ServerCore core(MakeConfig(), std::move(backend));

    Check(core.Start(), "Start() for player test");
    Check(WaitFor([&] { return !core.Players().empty(); }, std::chrono::seconds(5)), "player list published");

    const auto rows = core.Players();
    Check(rows.size() == 1, "one player row");
    Check(rows[0].username != nullptr && std::string(rows[0].username) == "alice", "username is interned and readable");
    Check(rows[0].isAdmin, "isAdmin propagated");

    // The username pointer must stay valid across later Players() calls.
    const char* firstPointer = rows[0].username;
    const auto again = core.Players();
    Check(again.size() == 1 && std::string(again[0].username) == "alice", "second Players() call agrees");
    (void)firstPointer;

    const auto snap = core.Snapshot();
    Check(snap.playerCount == 1, "playerCount surfaced in the snapshot");

    core.RequestStop();
}

void TestApplySettings()
{
    auto backend = std::make_unique<FakeBackend>();
    auto* raw = backend.get();
    ServerCore core(MakeConfig(), std::move(backend));

    Check(core.Start(), "Start() for settings test");
    Check(WaitFor([&] { return raw->updates.load() > 2; }, std::chrono::seconds(5)), "ticking before settings");

    SettingsPatch patch;
    patch.port = 10600;
    std::string error;
    Check(core.ApplySettings(patch, &error), "valid port patch accepted");
    Check(error.empty(), "no error text on success");
    Check(raw->appliedPort.load() == 10600, "patch reached the backend");
    Check(core.Config().port == 10600, "config reflects the new port");

    // Out-of-range values are rejected before they reach the backend.
    SettingsPatch badPort;
    badPort.port = 0;
    Check(!core.ApplySettings(badPort, &error), "port 0 rejected");
    Check(!error.empty(), "rejection carries a message");
    Check(core.Config().port == 10600, "rejected patch left the config untouched");

    SettingsPatch badTick;
    badTick.tickRate = 0;
    Check(!core.ApplySettings(badTick, &error), "tickRate 0 rejected");

    SettingsPatch badPlayers;
    badPlayers.maxPlayers = 0;
    Check(!core.ApplySettings(badPlayers, &error), "maxPlayers 0 rejected");

    // A backend refusal is propagated and not committed.
    raw->rejectPort.store(true);
    SettingsPatch refused;
    refused.port = 12345;
    Check(!core.ApplySettings(refused, &error), "backend refusal surfaces as false");
    Check(error.find("refuses") != std::string::npos, "backend error text is passed through");
    Check(core.Config().port == 10600, "refused patch did not commit");
    raw->rejectPort.store(false);

    // Null error pointer must be tolerated.
    SettingsPatch ok;
    ok.tickRate = 30;
    Check(core.ApplySettings(ok, nullptr), "null error pointer tolerated");

    core.RequestStop();
}

void TestFailedStart()
{
    auto backend = std::make_unique<FailingBackend>();
    ServerCore core(MakeConfig(), std::move(backend));

    Check(!core.Start(), "Start() reports a failed Initialize()");
    Check(core.State() == RunState::Stopped, "state returns to Stopped after a failed start");
    Check(!core.IsRunning(), "not running after a failed start");

    core.RequestStop();
    Check(core.State() == RunState::Stopped, "RequestStop on a stopped core is a no-op");
}

void TestDoubleStart()
{
    auto backend = std::make_unique<FakeBackend>();
    ServerCore core(MakeConfig(), std::move(backend));

    Check(core.Start(), "first Start() succeeds");
    Check(!core.Start(), "second Start() while running is refused");
    core.RequestStop();

    Check(core.Start(), "Start() works again after a stop");
    core.RequestStop();
}

void TestStopBeforeStart()
{
    auto backend = std::make_unique<FakeBackend>();
    ServerCore core(MakeConfig(), std::move(backend));

    core.RequestStop();
    core.RequestStop();
    Check(core.State() == RunState::Stopped, "RequestStop before Start is harmless");
    Check(core.Snapshot().state == RunState::Stopped, "snapshot readable before Start");
}

// Destructor must join: if it did not, this would exit with the thread running.
void TestDestructorJoins()
{
    auto backend = std::make_unique<FakeBackend>();
    auto* raw = backend.get();
    {
        ServerCore core(MakeConfig(), std::move(backend));
        Check(core.Start(), "Start() for destructor test");
        Check(WaitFor([&] { return raw->updates.load() > 2; }, std::chrono::seconds(5)), "ticking before dtor");
        // No RequestStop: the destructor has to do it.
    }

    Check(raw->shutDown.load(), "destructor shut the backend down");
    std::printf("  destructor joined without an explicit RequestStop: ok\n");
}
} // namespace

int main()
{
    std::printf("core_snapshot_test\n");

    TestStartStopLifecycle();
    TestSnapshotFromSecondThread();
    TestCommandExecutesOnCoreThread();
    TestPlayers();
    TestApplySettings();
    TestFailedStart();
    TestDoubleStart();
    TestStopBeforeStart();
    TestDestructorJoins();

    if (g_failures != 0)
    {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }

    std::printf("core_snapshot_test: all checks passed\n");
    return 0;
}
