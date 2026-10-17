#pragma once

// ServerCore: the UI-free facade over the dedicated server.
//
// Everything a control panel (Win32 today, anything later) needs is reachable
// through this class and nothing else: no spdlog, no windows.h, no uv.h, no
// GameServer.h. The real server is attached later through ICoreBackend, which
// is a thin adapter over DediRunner / IGameServerInstance.
//
// Threading contract:
//   * the core owns exactly one thread, started by Start() and joined by the
//     destructor. Nothing is detached.
//   * Snapshot() and Players() may be called from any thread at any time.
//   * SubmitCommand() queues work for the core thread; it never blocks on the
//     server and never touches server state.
//
// LogLevel is part of the shared serverui contract and is owned by the log
// stream's header (LogRing.h). This facade must also compile standalone - its
// test links only ServerCore.cpp and never touches the UI - so the log header
// is picked up when it is present and the enum is declared locally only when it
// is not. Two definitions in the same namespace would be a hard redefinition
// error, so this is deliberately not an unconditional local declaration.

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../metrics/MetricsSampler.h"

#if defined(__has_include)
#if __has_include("../log/LogRing.h")
#include "../log/LogRing.h"
#define SERVERUI_LOGLEVEL_AVAILABLE 1
#endif
#endif

namespace serverui
{
#ifndef SERVERUI_LOGLEVEL_AVAILABLE
// Standalone fallback: identical enumerators, in the same order, so a value
// crossing the seam means the same thing in both builds.
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical
};
#endif

enum class RunState
{
    Stopped,
    Starting,
    Running,
    Stopping
};

const char* ToString(RunState aState) noexcept;

struct CoreSnapshot
{
    RunState state{RunState::Stopped};
    uint64_t uptimeMs{0};
    uint16_t port{0};
    uint32_t playerCount{0};
    double tps{0.0};
    double frameTimeMs{0.0};

    // Live metrics, filled from metrics::MetricsSampler by the core thread.
    //
    // cpuPercent and memRssBytes describe THIS PROCESS. netIn/netOut/packets
    // describe the WHOLE MACHINE: Windows has no public per-process network
    // counter, so those four are the system-wide interface totals and a UI must
    // label them as machine-wide, not as this server's traffic. See
    // MetricsSampler.h for the exact definition and the limitations.
    //
    // A field is 0 only when it was not measured; it is never fabricated. The
    // sampler's own valid flags are the authority on that, and this struct does
    // not carry them, so a 0 here means "not measured" and must render as "-".
    //
    // cpuPercent is percent of ONE core (0 .. 100 * logicalCores), not a share
    // of the whole machine. On a 24-core host a single saturated thread reads
    // ~100, not ~4.
    double cpuPercent{0.0};
    uint64_t memRssBytes{0};
    uint64_t netInBytesPerSec{0};
    uint64_t netOutBytesPerSec{0};
    uint64_t packetsIn{0};
    uint64_t packetsOut{0};
};

struct PlayerRow
{
    uint32_t connectionId{0};
    uint32_t playerId{0};
    const char* username{nullptr}; // stable for the lifetime of the returned vector
    uint32_t pingMs{0};
    uint64_t connectedAtMs{0};
    bool isAdmin{false};
};

struct CoreConfig
{
    std::string iniPath;             // "config/STServer.ini" by default
    uint16_t port{10578};            // the port the client dials by default
    uint32_t maxPlayers{32};
    uint32_t tickRate{60};           // ticks per second
};

struct SettingsPatch
{
    std::optional<uint16_t> port;
    std::optional<uint32_t> maxPlayers;
    std::optional<uint32_t> tickRate;
    std::optional<LogLevel> logLevel;
};

// Value-typed player description handed over by the backend. Unlike PlayerRow
// it owns its name, so it is safe to hand across the core/UI thread boundary.
struct PlayerInfo
{
    uint32_t connectionId{0};
    uint32_t playerId{0};
    std::string username;
    uint32_t pingMs{0};
    uint64_t connectedAtMs{0};
    bool isAdmin{false};
};

// The seam to the real server. Mirrors IGameServerInstance (Initialize /
// Shutdown / IsListening / IsRunning / Update) and adds the two hooks the
// facade needs: command dispatch (DediRunner::HandleConsole) and live settings.
// Every method except the destructor is called on the core thread only.
struct ICoreBackend
{
    virtual ~ICoreBackend() = default;

    virtual bool Initialize() = 0;
    virtual void Update() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsListening() = 0;
    virtual uint32_t PlayerCount() = 0;

    // Console command drained from the facade's queue.
    virtual void OnCommand(std::string_view aLine) { (void)aLine; }

    // Apply whatever the backend can change while running. Returning false with
    // a message is the expected answer for a setting that needs a restart.
    virtual bool ApplySettings(const SettingsPatch& aPatch, std::string* apError)
    {
        (void)aPatch;
        if (apError != nullptr)
            apError->clear();
        return true;
    }

    virtual std::vector<PlayerInfo> Players() { return {}; }
};

class ServerCore
{
public:
    // The backend must outlive the ServerCore.
    ServerCore(const CoreConfig& acConfig, std::unique_ptr<ICoreBackend> apBackend);
    ~ServerCore();

    ServerCore(const ServerCore&) = delete;
    ServerCore& operator=(const ServerCore&) = delete;

    // Spawns the core thread and returns once the outcome is known: true only
    // when the backend's Initialize() succeeded and the state is Running.
    bool Start();

    // Graceful and idempotent: the first call asks the core thread to wind down
    // and waits for it to finish, later calls are no-ops. Safe to call from the
    // core thread itself (it then only raises the flag).
    void RequestStop();

    // Graceful restart of the backend on the same core thread.
    void RequestRestart();

    bool IsRunning() const noexcept;

    RunState State() const noexcept;

    // Lock free read of the last published snapshot; safe from the UI thread.
    CoreSnapshot Snapshot() const;

    // Queued to the core thread and executed there, never on the caller.
    void SubmitCommand(std::string_view aLine);

    // Snapshot copy of the player list, safe from the UI thread.
    std::vector<PlayerRow> Players() const;

    // Validates the patch, applies what can be applied live and forwards the
    // rest to the backend on the core thread. Never throws; a rejection carries
    // a message. Returns true when everything asked for was accepted.
    bool ApplySettings(const SettingsPatch& aPatch, std::string* apError);

    CoreConfig Config() const;

private:
    void CoreThreadMain();
    void DrainCommands();
    void PublishFrame(std::chrono::steady_clock::time_point aFrameStart, bool aRefreshPlayers);
    void RecordTickStamp(std::chrono::steady_clock::time_point aNow);
    double MeasureTps(std::chrono::steady_clock::time_point aNow) const;
    uint32_t EffectiveTickRate() const;
    void Post(std::function<void()>&& aWork);
    void WakeCore();
    bool ApplySettingsNow(const SettingsPatch& acPatch, std::string* apError);

    std::unique_ptr<ICoreBackend> m_backend;
    std::thread m_thread;

    std::atomic<RunState> m_state{RunState::Stopped};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_restartRequested{false};
    std::atomic<int> m_startOutcome{0}; // 0 = pending, 1 = running, -1 = failed
    std::atomic<uint32_t> m_tickRate{60};
    std::atomic<uint16_t> m_port{10578};
    std::atomic<uint32_t> m_maxPlayers{32};

    std::string m_iniPath;

    // Wake-up channel for the tick loop, so RequestStop does not have to wait
    // out a full tick period. Also carries the Start() completion signal.
    mutable std::mutex m_wakeMutex;
    std::condition_variable m_wakeCv;

    // Serialises the join in RequestStop / the destructor; std::thread::join from
    // two threads at once is undefined.
    std::mutex m_joinMutex;

    // Work queue, producer = any thread, consumer = core thread only.
    mutable std::mutex m_commandMutex;
    std::deque<std::function<void()>> m_commandQueue;
    std::atomic<uint32_t> m_pendingWork{0};

    // Published state, written by the core thread, read by any thread.
    std::atomic<std::shared_ptr<const CoreSnapshot>> m_snapshot;
    std::atomic<std::shared_ptr<const std::vector<PlayerInfo>>> m_players;

    // Core thread only. Sampled from PublishFrame; the sampler throttles
    // itself to kRecommendedIntervalMs, so calling it every tick is free.
    metrics::MetricsSampler m_metricsSampler;

    // Core thread only.
    std::chrono::steady_clock::time_point m_startedAt{};
    double m_lastFrameTimeMs{0.0};
    uint64_t m_tickIndex{0};

    static constexpr size_t kTpsWindow = 64;
    std::array<std::chrono::steady_clock::time_point, kTpsWindow> m_tickStamps{};
    size_t m_tickStampHead{0};
    size_t m_tickStampCount{0};

    // Username interning arena. PlayerRow carries a bare const char*, so the
    // storage has to outlive the vector returned by Players(), because
    // PlayerRow::username is a bare const char*.
    //
    // Two arenas in rotation, one per generation. Players() fills one and leaves
    // the other untouched, so the result handed out by the previous call stays
    // readable - which is exactly the documented lifetime - while the set of
    // strings stays bounded by the number of connected players instead of growing
    // with every rename and reconnect.
    //
    // A single arena that is cleared and refilled would NOT work: deque::clear()
    // invalidates the pointers previously handed out. That was verified with a
    // test, not assumed.
    mutable std::mutex m_nameMutex;
    mutable std::deque<std::string> m_nameArena[2];
    mutable std::unordered_map<uint32_t, size_t> m_nameIndex[2];
    mutable uint32_t m_nameArenaIndex{0};
};
} // namespace serverui
