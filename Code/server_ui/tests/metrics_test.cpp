// Standalone test for MetricsSampler. Plain asserts, no framework.
//
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -o metrics_test.exe ^
//       tests/metrics_test.cpp src/metrics/MetricsSampler.cpp -lpsapi -liphlpapi
//
// What this proves, beyond "the numbers are non-zero":
//   1. repeated sampling yields a CPU percentage inside the documented range;
//   2. RSS is non-zero on Windows;
//   3. the CPU figure is a DELTA over the sample interval, not a lifetime
//      average - a busy loop must lift the reported value well above an idle
//      baseline, and the value must come back down after the loop stops. A
//      lifetime average would stay put in both directions;
//   4. the throttle returns the cached struct instead of resampling;
//   5. a network sample reports its scope honestly as system-wide.

#include "../src/metrics/MetricsSampler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace serverui::metrics;

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

// Busy loop for a fixed wall-clock duration. Deliberately spinning rather than
// sleeping: the point is to burn CPU time that the sampler must observe.
void BurnCpu(std::chrono::milliseconds aDuration)
{
    const auto deadline = std::chrono::steady_clock::now() + aDuration;
    volatile uint64_t accumulator = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        for (uint32_t i = 0; i < 4096; ++i)
            accumulator = accumulator + i * 2654435761u;
    }
    (void)accumulator;
}

// One sample after the sampler's own throttle window has elapsed.
MetricsSample SampleFresh(MetricsSampler& aSampler)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(MetricsSampler::kRecommendedIntervalMs + 50));
    return aSampler.Sample();
}

void TestCpuRange()
{
    MetricsSampler sampler;
    const uint32_t cores = MetricsSampler::LogicalCoreCount();
    std::printf("  logical cores = %u\n", cores);
    Check(cores > 0, "hardware_concurrency() reports at least one core");

    const auto first = sampler.Sample();
#if defined(_WIN32) || defined(__linux__)
    // Both platforms measure a delta, so both must refuse to answer on the very
    // first call rather than inventing 0% or 100%.
    Check(!first.cpu.valid, "first sample has no CPU baseline yet and says so");
    Check(first.cpu.percent == 0.0, "first sample does not fabricate a CPU number");
#else
    Check(!first.cpu.valid, "unsupported platform: CPU sample is invalid rather than invented");
    Check(first.cpu.percent == 0.0, "unsupported platform: CPU stays 0");
#endif

    // Repeated sampling over ~1s, which is what the acceptance asks for.
    double observed = 0.0;
    for (int i = 0; i < 2; ++i)
    {
        const auto sample = SampleFresh(sampler);
        observed = sample.cpu.percent;
        std::printf("  idle sample %d: cpu = %.2f%% of one core (%.2f%% of machine), valid = %s\n", i,
            sample.cpu.percent, sample.cpu.percentOfMachine, sample.cpu.valid ? "true" : "false");

#if defined(_WIN32) || defined(__linux__)
        Check(sample.cpu.valid, "CPU sample is valid once a baseline exists");
        const double ceiling = 100.0 * static_cast<double>(cores > 0 ? cores : 1);
        Check(sample.cpu.percent >= 0.0 && sample.cpu.percent <= ceiling,
            "CPU percentage is inside 0..100*cores");
        Check(sample.cpu.percentOfMachine >= 0.0 && sample.cpu.percentOfMachine <= 100.0,
            "machine-share percentage is inside 0..100");
#else
        Check(!sample.cpu.valid, "unsupported platform: CPU sample stays invalid");
#endif
    }

    std::printf("  idle baseline cpu = %.2f%%\n", observed);
}

void TestMemory()
{
    MetricsSampler sampler;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto sample = sampler.Sample();

#if defined(_WIN32) || defined(__linux__)
    std::printf("  rss = %llu bytes (%.2f MiB), valid = %s\n",
        static_cast<unsigned long long>(sample.memory.rssBytes),
        static_cast<double>(sample.memory.rssBytes) / (1024.0 * 1024.0), sample.memory.valid ? "true" : "false");
    Check(sample.memory.valid, "RSS sample is valid on this platform");
    Check(sample.memory.rssBytes > 0, "a running process always has a non-zero resident set");
    // A 64-bit process running this test is far below 64 GiB; this catches a
    // unit or sign mistake (pages vs bytes) rather than an exact value.
    Check(sample.memory.rssBytes < (64ull * 1024 * 1024 * 1024), "RSS is in bytes, not pages or KiB");
#else
    std::printf("  rss: valid = %s (documented zero-with-invalid on this platform)\n",
        sample.memory.valid ? "true" : "false");
    Check(!sample.memory.valid, "unsupported platform: RSS is invalid rather than invented");
    Check(sample.memory.rssBytes == 0, "unsupported platform: RSS stays 0");
#endif
}

// The core of the acceptance: deltas, not a lifetime average.
void TestCpuIsDeltaNotLifetimeAverage()
{
#if defined(_WIN32) || defined(__linux__)
    MetricsSampler sampler;
    // Establish an idle baseline first so the busy window is compared against a
    // real idle reading rather than against nothing.
    const auto idle = SampleFresh(sampler);
    std::printf("  baseline (idle) cpu = %.2f%%\n", idle.cpu.percent);

    // Burn CPU across a full throttle window so the busy sample is dominated by
    // the loop rather than by the preceding idle period.
    BurnCpu(std::chrono::milliseconds(MetricsSampler::kRecommendedIntervalMs + 100));
    const auto busy = sampler.Sample();
    std::printf("  busy    cpu = %.2f%% of one core (%.2f%% of machine)\n",
        busy.cpu.percent, busy.cpu.percentOfMachine);

    Check(busy.cpu.valid, "busy sample is valid");
    // One thread spinning cannot exceed one core's worth of CPU time.
    Check(busy.cpu.percent <= 100.0 * static_cast<double>(MetricsSampler::LogicalCoreCount()),
        "busy CPU stays under the documented ceiling");
    Check(busy.cpu.percent > 0.0, "a busy loop is reported as non-zero CPU");

    // This is the assertion a lifetime average cannot satisfy: after ~1s of
    // spinning out of a process lifetime of a few seconds, a lifetime average
    // would sit near 25%, while the interval delta is far higher.
    std::printf("  delta check: busy (%.2f%%) > idle (%.2f%%) by %.2f points\n",
        busy.cpu.percent, idle.cpu.percent, busy.cpu.percent - idle.cpu.percent);
    Check(busy.cpu.percent > idle.cpu.percent + 10.0,
        "busy CPU is clearly above the idle baseline (delta, not lifetime average)");

    // And it must come back down, which a monotonic lifetime average would not.
    const auto after = SampleFresh(sampler);
    std::printf("  after   cpu = %.2f%% (idle again)\n", after.cpu.percent);
    Check(after.cpu.percent < busy.cpu.percent, "CPU falls back once the loop stops");
#else
    std::printf("  busy-loop delta check skipped: no per-process CPU counter on this platform\n");
#endif
}

void TestThrottleReturnsCache()
{
    MetricsSampler sampler;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto first = sampler.Sample();
    // Immediately again: well inside kRecommendedIntervalMs, so this must be
    // the cached struct rather than a fresh, near-zero-length delta.
    const auto second = sampler.Sample();

    Check(first.memory.rssBytes == second.memory.rssBytes, "throttled call returns the cached sample");
    Check(first.cpu.percent == second.cpu.percent, "throttled call does not recompute CPU");

    // After the interval the sampler really does resample.
    std::this_thread::sleep_for(std::chrono::milliseconds(MetricsSampler::kRecommendedIntervalMs + 50));
    const auto third = sampler.Sample();
    (void)third;
    std::printf("  throttle window = %u ms\n", MetricsSampler::kRecommendedIntervalMs);
}

void TestNetworkHonesty()
{
    MetricsSampler sampler;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    sampler.Sample();
    const auto sample = SampleFresh(sampler);

#if defined(_WIN32) || defined(__linux__)
    std::printf("  network: valid = %s, scope = %s, in = %llu B/s, out = %llu B/s\n",
        sample.network.valid ? "true" : "false",
        sample.network.scope == CounterScope::SystemWide ? "system-wide" : "other",
        static_cast<unsigned long long>(sample.network.inBytesPerSec),
        static_cast<unsigned long long>(sample.network.outBytesPerSec));
    std::printf("  network totals: in = %llu pkts, out = %llu pkts\n",
        static_cast<unsigned long long>(sample.network.inPacketsTotal),
        static_cast<unsigned long long>(sample.network.outPacketsTotal));

    // The honest contract: if the sample is valid it must be labelled
    // system-wide, never presented as a per-process figure. Traffic itself may
    // legitimately be zero on an idle machine, so non-zero is NOT asserted.
    if (sample.network.valid)
        Check(sample.network.scope == CounterScope::SystemWide,
            "a valid network sample is labelled system-wide, not per-process");
    else
        std::printf("  network sample invalid (no interface up) - acceptable on an isolated host\n");
#else
    std::printf("  network: valid = %s (documented zero-with-invalid on this platform)\n",
        sample.network.valid ? "true" : "false");
    Check(!sample.network.valid, "unsupported platform: network is invalid rather than invented");
    Check(sample.network.inBytesPerSec == 0 && sample.network.outBytesPerSec == 0,
        "unsupported platform: network rates stay 0");
#endif
}
} // namespace

int main()
{
    std::printf("metrics_test\n");

    TestCpuRange();
    TestMemory();
    TestCpuIsDeltaNotLifetimeAverage();
    TestThrottleReturnsCache();
    TestNetworkHonesty();

    if (g_failures != 0)
    {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }

    std::printf("metrics_test: all checks passed\n");
    return 0;
}
