#include "MetricsSampler.h"

#include <thread>

#if defined(_WIN32)
// winsock2.h must precede windows.h, otherwise windows.h pulls in the old
// winsock.h and the two headers redefine each other.
#include <winsock2.h>
// ws2ipdef.h must come before iphlpapi.h. iphlpapi.h includes netioapi.h, and
// netioapi.h only declares MIB_IF_ROW2 / MIB_IF_TABLE2 / GetIfEntry2 inside
// "#ifdef _WS2IPDEF_". netioapi.h has a plain include guard, so whichever
// inclusion happens first wins: if iphlpapi.h gets there before _WS2IPDEF_ is
// defined, the interface-level counters are silently absent for the whole
// translation unit and only the coarse MIB_IFROW API remains.
#include <ws2ipdef.h>
#include <windows.h>

#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#elif defined(__linux__)
// The parsing itself lives in LinuxProcParsers so it can be tested on any host;
// this translation unit only does the file reads.
#include "LinuxProcParsers.h"

#include <unistd.h>

#include <cstdio>
#include <cstring>
#endif

namespace serverui::metrics
{
namespace
{
#if defined(_WIN32)
uint64_t FileTimeToU64(const FILETIME& acTime) noexcept
{
    ULARGE_INTEGER value;
    value.LowPart = acTime.dwLowDateTime;
    value.HighPart = acTime.dwHighDateTime;
    return value.QuadPart;
}

// A virtual adapter that carries the same bytes as a physical one. Counting
// both would roughly double the reported traffic on a VPN, so they are skipped.
bool IsCountedInterface(const MIB_IF_ROW2& acRow) noexcept
{
    if (acRow.OperStatus != IfOperStatusUp)
        return false;
    if (acRow.Type == IF_TYPE_SOFTWARE_LOOPBACK)
        return false;
    if (acRow.Type == IF_TYPE_TUNNEL)
        return false;
    return true;
}
#elif defined(__linux__)
// Reads a whole /proc file into a fixed buffer and NUL-terminates it.
//
// Fixed capacity, no allocation, no exceptions: these files are a few hundred
// bytes (/proc/self/stat) up to a few kilobytes (/proc/net/dev with many
// adapters). A short read is not an error worth failing on - the parsers reject
// what they cannot use - so a truncated buffer simply yields a parsed prefix.
//
// Not std::ifstream: constructing one allocates and may throw, and this runs on
// the core tick thread.
bool ReadWholeFile(const char* acPath, char* apBuffer, size_t aCapacity) noexcept
{
    if (acPath == nullptr || apBuffer == nullptr || aCapacity < 2)
        return false;

    std::FILE* pFile = std::fopen(acPath, "rb");
    if (pFile == nullptr)
        return false;

    const size_t read = std::fread(apBuffer, 1, aCapacity - 1, pFile);
    std::fclose(pFile);

    apBuffer[read] = '\0';
    return read > 0;
}
#endif
} // namespace

uint32_t MetricsSampler::LogicalCoreCount() noexcept
{
    return std::thread::hardware_concurrency();
}

#if defined(_WIN32)
void MetricsSampler::EnsureInterfaceList() noexcept
{
    m_interfaceCount = 0;
    m_interfaceListValid = false;

    // GetIfTable2 allocates the table for us; this runs only on capture and on
    // rebuild, never in the steady state.
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || table == nullptr)
        return;

    for (ULONG i = 0; i < table->NumEntries && m_interfaceCount < kMaxInterfaces; ++i)
    {
        if (!IsCountedInterface(table->Table[i]))
            continue;

        m_interfaceIndex[m_interfaceCount] = table->Table[i].InterfaceIndex;
        ++m_interfaceCount;
    }

    FreeMibTable(table);

    // An empty list is a legitimate state (no adapter up), but it must not be
    // treated as a valid cache or every later sample would skip the rebuild.
    m_interfaceListValid = m_interfaceCount > 0;
}
#endif

MetricsSample MetricsSampler::Sample() noexcept
{
    const auto now = std::chrono::steady_clock::now();

    // Throttle: the numbers are deltas, so a caller hammering Sample() at tick
    // rate must not shrink the window to a few microseconds.
    if (m_hasBaseline)
    {
        const auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSampleAt);
        if (sinceLast.count() < static_cast<long long>(kRecommendedIntervalMs))
            return m_cached;
    }

    MetricsSample sample;
    const uint32_t cores = LogicalCoreCount();
    sample.cpu.logicalCores = cores;

    uint64_t wallUs = 0;
    if (m_hasBaseline)
    {
        wallUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastSampleAt).count());
    }

#if defined(_WIN32)
    // ---- CPU: per process, delta over wall clock -------------------------
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    uint64_t processCpu100ns = 0;
    bool cpuRead = false;
    if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime) != 0)
    {
        // Kernel time includes idle and wait time; that is what every task
        // manager reports, so it is the right thing to sum here.
        processCpu100ns = FileTimeToU64(kernelTime) + FileTimeToU64(userTime);
        cpuRead = true;
    }

    if (cpuRead && m_hasBaseline && wallUs > 0 && processCpu100ns >= m_prevProcessCpu100ns)
    {
        // cpu100ns / 1e7 == seconds of CPU; wallUs / 1e6 == seconds of wall.
        // 100 * (cpu100ns / 1e7) / (wallUs / 1e6) == 10 * cpu100ns / wallUs.
        const double raw = 10.0 * static_cast<double>(processCpu100ns - m_prevProcessCpu100ns) /
                           static_cast<double>(wallUs);
        const double ceiling = (cores > 0) ? 100.0 * static_cast<double>(cores) : 100.0;
        sample.cpu.percent = (raw < 0.0) ? 0.0 : (raw > ceiling ? ceiling : raw);
        sample.cpu.percentOfMachine = (cores > 0) ? (sample.cpu.percent / static_cast<double>(cores)) : 0.0;
        sample.cpu.valid = true;
    }
    if (cpuRead)
        m_prevProcessCpu100ns = processCpu100ns;

    // ---- Memory: per process resident set --------------------------------
    PROCESS_MEMORY_COUNTERS_EX memoryCounters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memoryCounters),
            sizeof(memoryCounters)) != 0 &&
        memoryCounters.WorkingSetSize > 0)
    {
        sample.memory.rssBytes = static_cast<uint64_t>(memoryCounters.WorkingSetSize);
        sample.memory.valid = true;
    }

    // ---- Network: SYSTEM WIDE interface totals ---------------------------
    if (!m_interfaceListValid)
        EnsureInterfaceList();

    uint64_t inBytes = 0;
    uint64_t outBytes = 0;
    uint64_t inPackets = 0;
    uint64_t outPackets = 0;
    bool anyInterface = false;

    for (uint32_t i = 0; i < m_interfaceCount; ++i)
    {
        MIB_IF_ROW2 row{};
        // GetIfEntry2 resolves the row by the InterfaceIndex the caller stores.
        row.InterfaceIndex = m_interfaceIndex[i];
        if (GetIfEntry2(&row) != NO_ERROR)
        {
            // The adapter vanished; rebuild the list on the next call.
            m_interfaceListValid = false;
            continue;
        }

        inBytes += row.InOctets;
        outBytes += row.OutOctets;
        inPackets += row.InUcastPkts + row.InNUcastPkts;
        outPackets += row.OutUcastPkts + row.OutNUcastPkts;
        anyInterface = true;
    }

    if (anyInterface)
    {
        sample.network.valid = true;
        sample.network.scope = CounterScope::SystemWide;
        sample.network.inPacketsTotal = inPackets;
        sample.network.outPacketsTotal = outPackets;

        // A total that went backwards means an adapter reset or left the set;
        // report 0 for this interval and rebaseline instead of underflowing.
        if (m_hasBaseline && wallUs > 0 && inBytes >= m_prevInBytes && outBytes >= m_prevOutBytes &&
            inPackets >= m_prevInPackets && outPackets >= m_prevOutPackets)
        {
            const double us = static_cast<double>(wallUs);
            sample.network.inBytesPerSec = static_cast<uint64_t>(
                static_cast<double>(inBytes - m_prevInBytes) * 1'000'000.0 / us);
            sample.network.outBytesPerSec = static_cast<uint64_t>(
                static_cast<double>(outBytes - m_prevOutBytes) * 1'000'000.0 / us);
            sample.network.inPacketsPerSec = static_cast<uint64_t>(
                static_cast<double>(inPackets - m_prevInPackets) * 1'000'000.0 / us);
            sample.network.outPacketsPerSec = static_cast<uint64_t>(
                static_cast<double>(outPackets - m_prevOutPackets) * 1'000'000.0 / us);
        }

        m_prevInBytes = inBytes;
        m_prevOutBytes = outBytes;
        m_prevInPackets = inPackets;
        m_prevOutPackets = outPackets;
    }
#elif defined(__linux__)
    // ---- CPU: per process, delta over wall clock -------------------------
    // /proc/self/stat fields 14 (utime) and 15 (stime), in USER_HZ ticks. Same
    // interval-based measurement as the Windows path, so the two platforms
    // report the same quantity.
    char statBuffer[1024];
    uint64_t userTicks = 0;
    uint64_t systemTicks = 0;
    bool cpuRead = false;

    if (ReadWholeFile("/proc/self/stat", statBuffer, sizeof(statBuffer)))
    {
        if (linuxproc::ParseSelfStat(statBuffer, std::strlen(statBuffer), &userTicks, &systemTicks))
            cpuRead = true;
    }

    // sysconf is not free but is stable; cache it in a function-local static.
    static const long ticksPerSecond = []() -> long
    {
        const long value = sysconf(_SC_CLK_TCK);
        return value > 0 ? value : 100; // Linux's own default
    }();

    const uint64_t processTicks = userTicks + systemTicks;

    if (cpuRead && m_hasBaseline && wallUs > 0 && processTicks >= m_prevProcessCpu100ns)
    {
        // ticks -> seconds is / ticksPerSecond, so the CPU seconds elapsed are
        // (deltaTicks / ticksPerSecond) and the wall seconds are wallUs / 1e6.
        const double cpuSeconds =
            static_cast<double>(processTicks - m_prevProcessCpu100ns) / static_cast<double>(ticksPerSecond);
        const double wallSeconds = static_cast<double>(wallUs) / 1'000'000.0;

        if (wallSeconds > 0.0)
        {
            const double raw = 100.0 * cpuSeconds / wallSeconds;
            const double ceiling = (cores > 0) ? 100.0 * static_cast<double>(cores) : 100.0;
            sample.cpu.percent = (raw < 0.0) ? 0.0 : (raw > ceiling ? ceiling : raw);
            sample.cpu.percentOfMachine = (cores > 0) ? (sample.cpu.percent / static_cast<double>(cores)) : 0.0;
            sample.cpu.valid = true;
        }
    }
    if (cpuRead)
        m_prevProcessCpu100ns = processTicks;

    // ---- Memory: per process resident set --------------------------------
    char statmBuffer[256];
    uint64_t residentPages = 0;
    if (ReadWholeFile("/proc/self/statm", statmBuffer, sizeof(statmBuffer)) &&
        linuxproc::ParseSelfStatm(statmBuffer, std::strlen(statmBuffer), &residentPages) && residentPages > 0)
    {
        static const uint64_t pageSize = []() -> uint64_t
        {
            const long value = sysconf(_SC_PAGESIZE);
            return value > 0 ? static_cast<uint64_t>(value) : 4096; // x86-64 default
        }();

        sample.memory.rssBytes = residentPages * pageSize;
        sample.memory.valid = true;
    }

    // ---- Network: SYSTEM WIDE interface totals ---------------------------
    // /proc/net/dev has the same shape as the Windows interface table and the
    // same caveats: machine-wide, includes every other process, and excludes
    // loopback so 127.0.0.1 clients contribute nothing.
    char netBuffer[8192];
    if (ReadWholeFile("/proc/net/dev", netBuffer, sizeof(netBuffer)))
    {
        linuxproc::NetDevTotals totals;
        if (linuxproc::ParseNetDev(netBuffer, std::strlen(netBuffer), &totals))
        {
            sample.network.valid = true;
            sample.network.scope = CounterScope::SystemWide;
            sample.network.inPacketsTotal = totals.inPackets;
            sample.network.outPacketsTotal = totals.outPackets;

            if (m_hasBaseline && wallUs > 0 && totals.inBytes >= m_prevInBytes &&
                totals.outBytes >= m_prevOutBytes && totals.inPackets >= m_prevInPackets &&
                totals.outPackets >= m_prevOutPackets)
            {
                const double us = static_cast<double>(wallUs);
                sample.network.inBytesPerSec = static_cast<uint64_t>(
                    static_cast<double>(totals.inBytes - m_prevInBytes) * 1'000'000.0 / us);
                sample.network.outBytesPerSec = static_cast<uint64_t>(
                    static_cast<double>(totals.outBytes - m_prevOutBytes) * 1'000'000.0 / us);
                sample.network.inPacketsPerSec = static_cast<uint64_t>(
                    static_cast<double>(totals.inPackets - m_prevInPackets) * 1'000'000.0 / us);
                sample.network.outPacketsPerSec = static_cast<uint64_t>(
                    static_cast<double>(totals.outPackets - m_prevOutPackets) * 1'000'000.0 / us);
            }

            m_prevInBytes = totals.inBytes;
            m_prevOutBytes = totals.outBytes;
            m_prevInPackets = totals.inPackets;
            m_prevOutPackets = totals.outPackets;
        }
    }
#else
    // Other platforms (macOS, BSD): no counter is reached from here. Every group
    // stays valid == false and every number stays 0 rather than being invented.
    (void)wallUs;
    sample.network.scope = CounterScope::None;
#endif

    m_lastSampleAt = now;
    m_hasBaseline = true;
    m_cached = sample;
    return sample;
}

} // namespace serverui::metrics
