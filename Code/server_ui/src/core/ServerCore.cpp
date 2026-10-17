#include "ServerCore.h"

#include <algorithm>
#include <utility>

namespace serverui
{
namespace
{
constexpr uint16_t kMinPort = 1;
constexpr uint16_t kMaxPort = 65535;
constexpr uint32_t kMinPlayers = 1;
constexpr uint32_t kMaxPlayers = 1024;
constexpr uint32_t kMinTickRate = 1;
constexpr uint32_t kMaxTickRate = 1000;
constexpr auto kPlayerRefreshInterval = std::chrono::milliseconds(500);
constexpr auto kSettingsWaitTimeout = std::chrono::seconds(2);

std::string DescribeRange(const char* acName, uint32_t aValue, uint32_t aMin, uint32_t aMax)
{
    std::string message = acName;
    message += " out of range: ";
    message += std::to_string(aValue);
    message += " (expected ";
    message += std::to_string(aMin);
    message += "..";
    message += std::to_string(aMax);
    message += ")";
    return message;
}
} // namespace

const char* ToString(RunState aState) noexcept
{
    switch (aState)
    {
    case RunState::Stopped: return "stopped";
    case RunState::Starting: return "starting";
    case RunState::Running: return "running";
    case RunState::Stopping: return "stopping";
    }

    return "unknown";
}

ServerCore::ServerCore(const CoreConfig& acConfig, std::unique_ptr<ICoreBackend> apBackend)
    : m_backend(std::move(apBackend))
    , m_tickRate(acConfig.tickRate)
    , m_port(acConfig.port)
    , m_maxPlayers(acConfig.maxPlayers)
    , m_iniPath(acConfig.iniPath)
{
    auto initial = std::make_shared<CoreSnapshot>();
    initial->state = RunState::Stopped;
    initial->port = acConfig.port;
    m_snapshot.store(std::move(initial));
    m_players.store(std::make_shared<const std::vector<PlayerInfo>>());
}

ServerCore::~ServerCore()
{
    RequestStop();

    // RequestStop already joined unless it was called from the core thread, in
    // which case the thread has since finished and this join is immediate.
    std::scoped_lock lock(m_joinMutex);
    if (m_thread.joinable())
        m_thread.join();
}

bool ServerCore::Start()
{
    // Already started (running, or winding down): starting twice is a caller bug
    // we answer instead of crashing on.
    if (m_thread.joinable())
        return false;

    m_stopRequested.store(false, std::memory_order_release);
    m_restartRequested.store(false, std::memory_order_release);
    m_startOutcome.store(0, std::memory_order_release);
    m_state.store(RunState::Starting, std::memory_order_release);

    try
    {
        m_thread = std::thread(&ServerCore::CoreThreadMain, this);
    }
    catch (...)
    {
        m_state.store(RunState::Stopped, std::memory_order_release);
        return false;
    }

    // Block until Initialize() has an answer, so that a failed start is visible
    // to the caller as a return value and not only as a log line.
    std::unique_lock lock(m_wakeMutex);
    m_wakeCv.wait(lock, [this] { return m_startOutcome.load(std::memory_order_acquire) != 0; });

    return m_startOutcome.load(std::memory_order_acquire) == 1;
}

void ServerCore::RequestStop()
{
    // Raising the flag is idempotent on its own; the join below is what needs
    // serialising.
    m_stopRequested.store(true, std::memory_order_release);
    WakeCore();

    std::scoped_lock lock(m_joinMutex);
    if (!m_thread.joinable())
        return;

    // Called from a command handler on the core thread: raising the flag is all
    // we may do, joining ourselves would deadlock. The loop notices on its next
    // iteration and whoever owns the object joins.
    if (std::this_thread::get_id() == m_thread.get_id())
        return;

    m_thread.join();
}

void ServerCore::RequestRestart()
{
    if (!m_thread.joinable() || !IsRunning())
        return;

    m_restartRequested.store(true, std::memory_order_release);
    WakeCore();
}

bool ServerCore::IsRunning() const noexcept
{
    return m_state.load(std::memory_order_acquire) == RunState::Running;
}

RunState ServerCore::State() const noexcept
{
    return m_state.load(std::memory_order_acquire);
}

CoreSnapshot ServerCore::Snapshot() const
{
    // One atomic load of a shared_ptr: no lock, no tearing, safe from the UI
    // thread while the core thread publishes a new frame.
    const auto published = m_snapshot.load(std::memory_order_acquire);
    if (!published)
        return CoreSnapshot{};

    return *published;
}

void ServerCore::SubmitCommand(std::string_view aLine)
{
    if (!IsRunning())
        return;

    // Copied into the closure: the caller's view dies with its own statement.
    Post([this, line = std::string(aLine)]() {
        if (m_backend)
            m_backend->OnCommand(line);
    });
    WakeCore();
}

std::vector<PlayerRow> ServerCore::Players() const
{
    const auto published = m_players.load(std::memory_order_acquire);
    if (!published)
        return {};

    std::vector<PlayerRow> rows;
    rows.reserve(published->size());

    // Two-arena rotation.
    //
    // The contract is that username points at storage valid until the NEXT call
    // to Players(); PlayerCellAt() copies it into a std::string immediately. The
    // previous append-only arena satisfied that but grew without bound: every
    // rename, reconnect and departed player left a string behind forever.
    //
    // Clearing and refilling one arena would be wrong - deque::clear() DOES
    // invalidate the pointers previously handed out (verified, not assumed), so a
    // caller still holding the previous result would read freed strings. Instead
    // the arena that was current when the previous result was built is kept
    // untouched, and the other one is reused for this call. That gives exactly
    // one generation of overlap, which is what the contract promises.
    std::scoped_lock lock(m_nameMutex);

    // The generation handed out last time must survive this call; the one before
    // it is free to be reused.
    std::deque<std::string>& arena = m_nameArena[m_nameArenaIndex];
    std::unordered_map<uint32_t, size_t>& index = m_nameIndex[m_nameArenaIndex];
    arena.clear();
    index.clear();

    // Flip for the next call, which will need the other arena.
    m_nameArenaIndex ^= 1u;

    for (const auto& player : *published)
    {
        arena.push_back(player.username);
        const size_t slot = arena.size() - 1;
        index[player.connectionId] = slot;

        PlayerRow row;
        row.connectionId = player.connectionId;
        row.playerId = player.playerId;
        row.username = arena[slot].c_str();
        row.pingMs = player.pingMs;
        row.connectedAtMs = player.connectedAtMs;
        row.isAdmin = player.isAdmin;
        rows.push_back(row);
    }

    return rows;
}

bool ServerCore::ApplySettings(const SettingsPatch& acPatch, std::string* apError)
{
    if (apError != nullptr)
        apError->clear();

    const bool onCoreThread = m_thread.joinable() && std::this_thread::get_id() == m_thread.get_id();

    if (onCoreThread || !IsRunning())
    {
        // Either we are already the thread that owns the backend, or there is no
        // running backend to talk to; in both cases apply in place.
        return ApplySettingsNow(acPatch, apError);
    }

    // Marshal onto the core thread and wait for the verdict, so the caller gets
    // a real answer instead of a fire-and-forget queue entry.
    auto promise = std::make_shared<std::promise<std::pair<bool, std::string>>>();
    auto future = promise->get_future();

    Post([this, acPatch, promise]() {
        std::string error;
        bool applied = false;
        try
        {
            applied = ApplySettingsNow(acPatch, &error);
        }
        catch (...)
        {
            error = "backend threw while applying settings";
            applied = false;
        }

        promise->set_value({applied, error});
    });
    WakeCore();

    if (future.wait_for(kSettingsWaitTimeout) != std::future_status::ready)
    {
        if (apError != nullptr)
            *apError = "core thread did not respond in time";
        return false;
    }

    const auto result = future.get();
    if (apError != nullptr)
        *apError = result.second;

    return result.first;
}

CoreConfig ServerCore::Config() const
{
    CoreConfig config;
    config.iniPath = m_iniPath;
    config.port = m_port.load(std::memory_order_acquire);
    config.maxPlayers = m_maxPlayers.load(std::memory_order_acquire);
    config.tickRate = m_tickRate.load(std::memory_order_acquire);
    return config;
}

bool ServerCore::ApplySettingsNow(const SettingsPatch& acPatch, std::string* apError)
{
    auto reject = [apError](std::string aMessage) {
        if (apError != nullptr)
            *apError = std::move(aMessage);
        return false;
    };

    if (acPatch.port.has_value() && (*acPatch.port < kMinPort))
        return reject(DescribeRange("port", *acPatch.port, kMinPort, kMaxPort));

    if (acPatch.maxPlayers.has_value() && (*acPatch.maxPlayers < kMinPlayers || *acPatch.maxPlayers > kMaxPlayers))
        return reject(DescribeRange("maxPlayers", *acPatch.maxPlayers, kMinPlayers, kMaxPlayers));

    if (acPatch.tickRate.has_value() && (*acPatch.tickRate < kMinTickRate || *acPatch.tickRate > kMaxTickRate))
        return reject(DescribeRange("tickRate", *acPatch.tickRate, kMinTickRate, kMaxTickRate));

    // The backend has the last word: a port change needs a socket rebind, and it
    // may legitimately refuse while players are connected.
    if (IsRunning() && m_backend != nullptr)
    {
        std::string backendError;
        if (!m_backend->ApplySettings(acPatch, &backendError))
            return reject(backendError.empty() ? std::string("backend rejected the settings") : backendError);
    }

    // Only commit after the backend accepted, so a refusal leaves the published
    // config untouched.
    if (acPatch.port.has_value())
        m_port.store(*acPatch.port, std::memory_order_release);
    if (acPatch.maxPlayers.has_value())
        m_maxPlayers.store(*acPatch.maxPlayers, std::memory_order_release);
    if (acPatch.tickRate.has_value())
        m_tickRate.store(*acPatch.tickRate, std::memory_order_release);

    return true;
}

void ServerCore::Post(std::function<void()>&& aWork)
{
    {
        std::scoped_lock lock(m_commandMutex);

        // The counter is raised INSIDE the same critical section that appends,
        // and before the item becomes visible to DrainCommands. Raising it after
        // the push would leave a window in which the consumer swaps the queue,
        // executes and decrements a counter that was never incremented: the
        // unsigned counter would wrap to ~4 billion, the wake predicate would
        // stay true forever and the core thread would spin at 100% CPU.
        m_pendingWork.fetch_add(1, std::memory_order_acq_rel);
        m_commandQueue.push_back(std::move(aWork));
    }
}

void ServerCore::WakeCore()
{
    // The lock is what makes the signal impossible to lose: a waiter checks its
    // predicate while holding m_wakeMutex and only then parks, so a notifier that
    // takes the same lock can never fire in the window between the two.
    // notify_all, not notify_one: the core thread and a Start()/ApplySettings()
    // waiter may both be parked here. No caller of WakeCore holds the lock.
    {
        std::scoped_lock lock(m_wakeMutex);
    }
    m_wakeCv.notify_all();
}

void ServerCore::DrainCommands()
{
    std::deque<std::function<void()>> pending;
    {
        std::scoped_lock lock(m_commandMutex);
        pending.swap(m_commandQueue);
    }

    for (auto& work : pending)
    {
        try
        {
            work();
        }
        catch (...)
        {
            // A command handler must not take the server down.
        }

        m_pendingWork.fetch_sub(1, std::memory_order_acq_rel);
    }
}

uint32_t ServerCore::EffectiveTickRate() const
{
    const uint32_t rate = m_tickRate.load(std::memory_order_acquire);
    return std::clamp(rate, kMinTickRate, kMaxTickRate);
}

void ServerCore::RecordTickStamp(std::chrono::steady_clock::time_point aNow)
{
    m_tickStamps[m_tickStampHead] = aNow;
    m_tickStampHead = (m_tickStampHead + 1) % kTpsWindow;
    if (m_tickStampCount < kTpsWindow)
        ++m_tickStampCount;
}

double ServerCore::MeasureTps(std::chrono::steady_clock::time_point aNow) const
{
    (void)aNow;

    if (m_tickStampCount < 2)
        return 0.0;

    const size_t oldestSlot = (m_tickStampHead + kTpsWindow - m_tickStampCount) % kTpsWindow;
    const size_t newestSlot = (m_tickStampHead + kTpsWindow - 1) % kTpsWindow;

    const double seconds = std::chrono::duration<double>(m_tickStamps[newestSlot] - m_tickStamps[oldestSlot]).count();
    if (seconds <= 0.0)
        return 0.0;

    // Measured, not assumed: intervals actually elapsed between real ticks.
    return static_cast<double>(m_tickStampCount - 1) / seconds;
}

void ServerCore::PublishFrame(std::chrono::steady_clock::time_point aFrameEnd, bool aRefreshPlayers)
{
    // Sampled FIRST, before anything is published. The sample can cost a few
    // hundred microseconds on the tick that captures the interface list, and
    // PublishFrame has a strict ordering contract: m_players is stored before
    // m_snapshot, so any delay between the two would let a reader observe the
    // new player list next to the previous snapshot. Keeping the sampling ahead
    // of every store leaves that window exactly as it was.
    const metrics::MetricsSample metrics = m_metricsSampler.Sample();

    auto snapshot = std::make_shared<CoreSnapshot>();
    snapshot->state = m_state.load(std::memory_order_acquire);
    snapshot->port = m_port.load(std::memory_order_acquire);
    snapshot->tps = MeasureTps(aFrameEnd);
    snapshot->frameTimeMs = m_lastFrameTimeMs;

    if (m_startedAt != std::chrono::steady_clock::time_point{})
    {
        snapshot->uptimeMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(aFrameEnd - m_startedAt).count());
    }

    if (m_backend != nullptr)
    {
        try
        {
            snapshot->playerCount = m_backend->PlayerCount();
        }
        catch (...)
        {
            snapshot->playerCount = 0;
        }

        if (aRefreshPlayers)
        {
            try
            {
                m_players.store(std::make_shared<const std::vector<PlayerInfo>>(m_backend->Players()),
                    std::memory_order_release);
            }
            catch (...)
            {
                // Keep the previous list rather than publishing garbage.
            }
        }
    }

    // Live metrics, from the sample taken at the top of this function. The
    // sampler throttles itself to kRecommendedIntervalMs, so on most ticks this
    // is the cached struct. It never throws, never blocks and never invents a
    // number: an invalid group simply leaves its fields at 0, which the UI must
    // render as "not measured".
    //
    // cpuPercent and memRssBytes are per process; net* and packets* are
    // system-wide because no public per-process network counter exists.
    if (metrics.cpu.valid)
        snapshot->cpuPercent = metrics.cpu.percent;
    if (metrics.memory.valid)
        snapshot->memRssBytes = metrics.memory.rssBytes;
    if (metrics.network.valid)
    {
        snapshot->netInBytesPerSec = metrics.network.inBytesPerSec;
        snapshot->netOutBytesPerSec = metrics.network.outBytesPerSec;
        snapshot->packetsIn = metrics.network.inPacketsTotal;
        snapshot->packetsOut = metrics.network.outPacketsTotal;
    }

    m_snapshot.store(std::move(snapshot), std::memory_order_release);
}

void ServerCore::CoreThreadMain()
{
    bool initialized = false;
    try
    {
        initialized = (m_backend != nullptr) && m_backend->Initialize();
    }
    catch (...)
    {
        initialized = false;
    }

    if (!initialized)
    {
        m_state.store(RunState::Stopped, std::memory_order_release);
        m_startOutcome.store(-1, std::memory_order_release);
        WakeCore();
        return;
    }

    m_startedAt = std::chrono::steady_clock::now();
    m_lastFrameTimeMs = 0.0;
    m_tickStampHead = 0;
    m_tickStampCount = 0;
    m_tickIndex = 0;

    // Prime the metrics sampler before the state flips to Running. Its first
    // call is the expensive one - it captures the interface list and takes the
    // CPU/network baseline - and doing it here keeps that cost out of the first
    // PublishFrame. That matters because Start() returns as soon as the state is
    // Running, while the first published snapshot only appears on the first
    // frame; a slow first frame widens the window in which a reader can observe
    // a Running core next to the previous Stopped snapshot.
    m_metricsSampler.Sample();

    m_state.store(RunState::Running, std::memory_order_release);
    m_startOutcome.store(1, std::memory_order_release);
    WakeCore();

    auto nextPlayerRefresh = m_startedAt;

    // Absolute tick schedule, advanced by a fixed period instead of being
    // recomputed from "now" on every pass. A relative deadline (now + period)
    // silently drifts: Windows' default timer granularity is 15.625 ms, so a
    // 16.67 ms (60 Hz) relative wait rounds up to 31.25 ms and the loop settles
    // at half the configured rate. Anchoring to an absolute schedule keeps the
    // long-run rate exact, and when the loop falls behind it catches up by
    // firing without sleeping rather than by stretching the period.
    //
    // Recomputed every iteration because ApplySettings can change tickRate
    // while the server runs.
    auto period = std::chrono::nanoseconds(1'000'000'000LL / static_cast<long long>(EffectiveTickRate()));
    auto nextTick = m_startedAt + period;

    while (!m_stopRequested.load(std::memory_order_acquire))
    {
        if (m_restartRequested.exchange(false, std::memory_order_acq_rel))
        {
            bool restarted = false;
            try
            {
                m_backend->Shutdown();
                restarted = m_backend->Initialize();
            }
            catch (...)
            {
                restarted = false;
            }

            if (!restarted)
            {
                m_stopRequested.store(true, std::memory_order_release);
                break;
            }

            // Uptime and TPS describe the new run, not the old one.
            m_startedAt = std::chrono::steady_clock::now();
            m_tickStampHead = 0;
            m_tickStampCount = 0;
            nextPlayerRefresh = m_startedAt;
            nextTick = m_startedAt + period;
        }

        DrainCommands();

        const auto frameStart = std::chrono::steady_clock::now();
        try
        {
            m_backend->Update();
        }
        catch (...)
        {
            // A backend that throws must not kill the loop; the state stays
            // Running and the failure surfaces through the log ring.
        }
        const auto frameEnd = std::chrono::steady_clock::now();
        m_lastFrameTimeMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();

        RecordTickStamp(frameEnd);
        ++m_tickIndex;

        const bool refreshPlayers = frameEnd >= nextPlayerRefresh;
        if (refreshPlayers)
            nextPlayerRefresh = frameEnd + kPlayerRefreshInterval;

        PublishFrame(frameEnd, refreshPlayers);

        // Pick up a tick rate changed by ApplySettings. The schedule is
        // re-anchored so the new period takes effect immediately instead of
        // after the next slot.
        const auto newPeriod = std::chrono::nanoseconds(1'000'000'000LL / static_cast<long long>(EffectiveTickRate()));
        if (newPeriod != period)
        {
            period = newPeriod;
            nextTick = frameStart + period;
        }

        // Advance the schedule before sleeping. If the frame overran its slot the
        // deadline is already in the past and the wait returns at once, which is
        // the catch-up path: the loop makes up the lost ticks instead of quietly
        // lowering the tick rate.
        nextTick += period;

        // Sleep until the next scheduled tick, waking early for a stop, a restart
        // or a queued command so a SubmitCommand is not delayed by a full period.
        std::unique_lock lock(m_wakeMutex);
        m_wakeCv.wait_until(lock, nextTick, [this] {
            return m_stopRequested.load(std::memory_order_acquire) ||
                   m_restartRequested.load(std::memory_order_acquire) ||
                   m_pendingWork.load(std::memory_order_acquire) > 0;
        });

        // A stop, restart or command woke us before the deadline: keep the
        // schedule anchored, but do not let a long pause bank thousands of
        // catch-up ticks that would then fire in a burst.
        if (std::chrono::steady_clock::now() > nextTick + period)
            nextTick = std::chrono::steady_clock::now();
    }

    m_state.store(RunState::Stopping, std::memory_order_release);
    try
    {
        m_backend->Shutdown();
    }
    catch (...)
    {
        // Shutdown failures are not recoverable here and must not escape.
    }

    // Final frame: state Stopped and the live metrics zeroed, so a UI that keeps
    // polling after RequestStop sees "stopped", not a frozen running frame.
    auto finalFrame = std::make_shared<CoreSnapshot>();
    finalFrame->state = RunState::Stopped;
    finalFrame->port = m_port.load(std::memory_order_acquire);
    if (m_startedAt != std::chrono::steady_clock::time_point{})
    {
        finalFrame->uptimeMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_startedAt).count());
    }
    m_snapshot.store(std::move(finalFrame), std::memory_order_release);

    m_state.store(RunState::Stopped, std::memory_order_release);
    WakeCore();
}
} // namespace serverui
