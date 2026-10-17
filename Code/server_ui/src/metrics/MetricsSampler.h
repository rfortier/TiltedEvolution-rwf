#pragma once

// MetricsSampler - real OS metrics for the dedicated server dashboard.
//
// Replaces the hardcoded zeros that CoreSnapshot carried for cpuPercent,
// memRssBytes, netInBytesPerSec, netOutBytesPerSec, packetsIn and packetsOut.
//
// WHAT EACH COUNTER ACTUALLY MEASURES
// -----------------------------------
// cpu.percent        PER PROCESS, on Windows and Linux alike. The CPU-time
//                    delta is divided by the wall-clock delta between two reads,
//                    so the number describes the interval just elapsed and never
//                    the process lifetime. Windows uses GetProcessTimes;
//                    Linux uses /proc/self/stat fields 14/15 scaled by
//                    sysconf(_SC_CLK_TCK).
//                    Units are "percent of ONE core": 100.0 means one core fully
//                    busy, 100.0 * logicalCores means the whole machine. Range
//                    is 0 .. 100 * logicalCores; it cannot exceed that, because
//                    N threads cannot burn more than N seconds of CPU per
//                    second of wall time.
// cpu.percentOfMachine  The same figure divided by logicalCores, i.e. the
//                    share of the whole machine, range 0 .. 100. Both are
//                    reported because they answer different questions and a UI
//                    that shows the wrong one is misleading: 6.25% on a 16-core
//                    box looks idle under the second convention but means one
//                    thread pinned under the first. logicalCores is 0 when the
//                    platform cannot report it; percentOfMachine is then 0.
//                    GetSystemTimes was considered and rejected: it reports
//                    MACHINE-WIDE CPU, a different quantity, and reporting it
//                    under the name cpuPercent would be a lie.
//
// memory.rssBytes    PER PROCESS. The resident set: pages currently in physical
//                    memory. A snapshot, not a delta. It includes shared pages
//                    and the mapped libraries, so it is not "the server's heap".
//                    Windows: GetProcessMemoryInfo -> WorkingSetSize.
//                    Linux: /proc/self/statm field 2 * sysconf(_SC_PAGESIZE).
//
// network.*          SYSTEM WIDE, NOT PER PROCESS. This is the important
//                    caveat. Windows exposes no public per-process network byte
//                    or packet counter; the per-process NT/ETW internals are
//                    not a supported API and are deliberately not used here.
//                    What is measured instead is the sum over every network
//                    interface that is operationally up, excluding
//                    software-loopback and tunnel adapters (a VPN would
//                    otherwise count the same bytes twice, once on the virtual
//                    adapter and once on the physical NIC):
//                      inBytes   += InOctets
//                      outBytes  += OutOctets
//                      inPackets += InUcastPkts  + InNUcastPkts
//                      outPackets+= OutUcastPkts + OutNUcastPkts
//                    Consequences a UI must not hide:
//                      * the figure includes every other process on the machine
//                        - browsers, Steam downloads, Windows Update;
//                      * the server's own GameNetworkingSockets UDP traffic is
//                        included but is NOT separable from the rest;
//                      * loopback traffic is not counted at all, so a client
//                        connected over 127.0.0.1 contributes nothing;
//                      * the interface list is captured once and rebuilt only
//                        when a cached interface stops resolving, so an adapter
//                        that appears later is not picked up until that happens.
//                    network.scope therefore reports CounterScope::SystemWide,
//                    and a caller rendering these numbers is expected to label
//                    them machine-wide.
//
//                    Linux reads the same four totals from /proc/net/dev, with
//                    the same exclusions (loopback only there; tunnels cannot be
//                    identified from that file, so a VPN's virtual adapter IS
//                    counted on Linux - a known asymmetry with Windows). The
//                    parsing lives in LinuxProcParsers so it is testable on any
//                    host. Other platforms leave every network field 0 with
//                    valid == false.
//
// VALIDITY
// --------
// Every sub-sample carries a valid flag and no field is ever fabricated:
//   * cpu.valid is false on the very first call, because a delta needs two
//     readings. percent stays 0.0 in that case.
//   * memory.valid is false only when the OS call failed or reported 0.
//   * network.valid is false on non-Windows builds and when no interface could
//     be read. On the first call the rates are 0 (no baseline yet) while the
//     packet totals are already real, so valid is true.
// A caller must render valid == false as "-", never as 0.
//
// COST AND CALL INTERVAL
// ----------------------
// One sample is a handful of cheap syscalls and allocates nothing per call. It
// is cheap but pointless at tick rate, so Sample() throttles itself: a call
// made less than kRecommendedIntervalMs after the previous real sample returns
// the cached struct untouched. Calling Sample() on every 60 Hz tick therefore
// costs a clock read, and the numbers refresh at 2 Hz. Do not go much below
// 250 ms: the CPU figure is a ratio of two small deltas and gets noisy.
//
// THREADING
// ---------
// Single owner, no synchronisation. An instance holds mutable state (the
// previous CPU time and the previous network totals) and must be sampled from
// one thread only - in this tree that is the ServerCore core thread. Give each
// thread its own instance; instances share nothing, not even static state.
//
// LINK REQUIREMENTS
// -----------------
// Windows: psapi.lib and iphlpapi.lib (MinGW: -lpsapi -liphlpapi).
//   MinGW's psapi.h defaults to PSAPI_VERSION 2, where GetProcessMemoryInfo is
//   #defined to K32GetProcessMemoryInfo and resolves from kernel32, so -lpsapi
//   is belt-and-braces there rather than strictly required. Keep it: with
//   -DPSAPI_VERSION=1 the symbol really does live in psapi.
//   iphlpapi is genuinely required (GetIfTable2 / GetIfEntry2 / FreeMibTable).
// Other platforms: no extra libraries; the whole Windows path compiles out.

#include <chrono>
#include <cstdint>

namespace serverui::metrics
{

// What a group of numbers describes. Process and SystemWide are not
// interchangeable and a UI that renders one as the other is misleading.
enum class CounterScope
{
    None,
    Process,
    SystemWide
};

struct CpuSample
{
    bool valid{false};
    // Percent of ONE core, measured over the interval since the previous
    // sample. Range 0 .. 100 * logicalCores.
    double percent{0.0};
    // The same measurement as a share of the whole machine. Range 0 .. 100.
    double percentOfMachine{0.0};
    // std::thread::hardware_concurrency(): logical processors, 0 if unknown.
    uint32_t logicalCores{0};
};

struct MemorySample
{
    bool valid{false};
    // Resident set size in bytes (physical memory currently backing the process).
    uint64_t rssBytes{0};
};

struct NetworkSample
{
    bool valid{false};
    CounterScope scope{CounterScope::None};

    // Rates over the interval since the previous sample. 0 on the first sample.
    uint64_t inBytesPerSec{0};
    uint64_t outBytesPerSec{0};
    uint64_t inPacketsPerSec{0};
    uint64_t outPacketsPerSec{0};

    // Cumulative counters since the interface was brought up (effectively since
    // boot). These are what the raw OS counters hold; the contract fields
    // CoreSnapshot::packetsIn / packetsOut are filled from them.
    uint64_t inPacketsTotal{0};
    uint64_t outPacketsTotal{0};
};

struct MetricsSample
{
    CpuSample cpu;
    MemorySample memory;
    NetworkSample network;
};

class MetricsSampler
{
public:
    // See "COST AND CALL INTERVAL". 500 ms gives stable CPU deltas at 2 Hz.
    static constexpr uint32_t kRecommendedIntervalMs = 500;

    MetricsSampler() noexcept = default;

    MetricsSampler(const MetricsSampler&) = delete;
    MetricsSampler& operator=(const MetricsSampler&) = delete;

    // Never throws, never blocks, never allocates per call. Returns the cached
    // struct when called sooner than kRecommendedIntervalMs after the last real
    // sample. Safe to call every tick.
    MetricsSample Sample() noexcept;

    // std::thread::hardware_concurrency(): logical processors, 0 if unknown.
    static uint32_t LogicalCoreCount() noexcept;

private:
#if defined(_WIN32)
    // Interface list captured once; rebuilt only when a cached index stops
    // resolving. Fixed capacity keeps the steady-state sample allocation free.
    static constexpr uint32_t kMaxInterfaces = 32;
    void EnsureInterfaceList() noexcept;

    uint32_t m_interfaceIndex[kMaxInterfaces]{};
    uint32_t m_interfaceCount{0};
    bool m_interfaceListValid{false};
#endif

    // True once a baseline exists; also the throttle state.
    bool m_hasBaseline{false};
    std::chrono::steady_clock::time_point m_lastSampleAt{};
    uint64_t m_prevProcessCpu100ns{0};
    uint64_t m_prevInBytes{0};
    uint64_t m_prevOutBytes{0};
    uint64_t m_prevInPackets{0};
    uint64_t m_prevOutPackets{0};
    MetricsSample m_cached{};
};

} // namespace serverui::metrics
