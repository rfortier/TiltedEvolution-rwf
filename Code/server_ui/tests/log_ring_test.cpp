// log_ring_test.cpp - acceptance tests for serverui::LogRing.
// Plain CHECK macro, no gtest. Returns non-zero on the first failure.

#include "../src/log/LogRing.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
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

constexpr char kFiller[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEF";
constexpr size_t kFillerLen = sizeof(kFiller) - 1;
constexpr int kProducers = 4;
constexpr int kPerProducer = 100000;

// Validates one drained record against the producer encoding
// "P<tid>-<idx>-<filler>". Catches torn, truncated, mixed and reordered records.
struct Validation
{
    uint64_t badText{0};
    uint64_t badOrder{0};
    uint64_t badSeq{0};
    uint64_t lastSeq{0};
    bool haveSeq{false};
    std::vector<long long> lastIndex;

    explicit Validation(size_t aProducers) : lastIndex(aProducers, -1) {}

    void Check(const LogRecord& acRec)
    {
        if (haveSeq && acRec.seq != lastSeq + 1)
            ++badSeq;
        lastSeq = acRec.seq;
        haveSeq = true;

        if (acRec.text == nullptr || acRec.len != std::strlen(acRec.text) ||
            acRec.len > LogRing::kPayloadBytes)
        {
            ++badText;
            return;
        }

        unsigned tid = 0;
        long long idx = -1;
        if (std::sscanf(acRec.text, "P%u-%lld", &tid, &idx) != 2)
        {
            ++badText;
            return;
        }

        if (acRec.len < kFillerLen ||
            std::memcmp(acRec.text + acRec.len - kFillerLen, kFiller, kFillerLen) != 0)
        {
            ++badText;
            return;
        }

        if (tid >= lastIndex.size())
        {
            ++badText;
            return;
        }

        // Per-producer sequence must strictly increase: proves no reordering.
        if (idx <= lastIndex[tid])
            ++badOrder;
        lastIndex[tid] = idx;
    }
};

// ---------------------------------------------------------------------------
// (a) 4 producer threads x 100k + 1 concurrent consumer thread.
// ---------------------------------------------------------------------------
void TestConcurrent()
{
    const size_t capacityBytes = 1u << 20;
    LogRing ring(capacityBytes);
    std::printf("[a] ring capacity = %zu bytes -> %zu slots\n", capacityBytes, ring.SlotCount());

    std::atomic<bool> allDone{false};
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> overMax{0};
    constexpr size_t kMaxPerDrain = 256;

    std::thread consumer([&]
    {
        std::vector<LogRecord> buf(kMaxPerDrain);
        Validation v(kProducers);

        for (;;)
        {
            const size_t n = ring.Drain(buf.data(), kMaxPerDrain);
            if (n > kMaxPerDrain)
                overMax.fetch_add(1);

            if (n == 0)
            {
                if (allDone.load(std::memory_order_acquire) && ring.Drain(buf.data(), kMaxPerDrain) == 0)
                    break;
                std::this_thread::yield();
                continue;
            }

            for (size_t i = 0; i < n; ++i)
                v.Check(buf[i]);
            received.fetch_add(n);
        }

        // Publish the validation outcome through the atomics we assert on later.
        if (v.badText || v.badOrder || v.badSeq)
            overMax.fetch_add(1000);
    });

    const auto t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&ring, p]
        {
            char buf[128];
            for (int i = 0; i < kPerProducer; ++i)
            {
                const int n = std::snprintf(buf, sizeof(buf), "P%d-%d-%s", p, i, kFiller);
                ring.Push(LogLevel::Info, static_cast<uint32_t>(p),
                    static_cast<uint64_t>(i), buf, static_cast<size_t>(n));
            }
        });
    }

    for (std::thread& t : producers)
        t.join();
    allDone.store(true, std::memory_order_release);
    consumer.join();

    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    const uint64_t pushed = static_cast<uint64_t>(kProducers) * kPerProducer;
    const uint64_t total = ring.TotalCount();
    const uint64_t dropped = ring.DroppedCount();
    const uint64_t got = received.load();

    std::printf("[a] pushed=%llu total=%llu received=%llu dropped=%llu wall=%.1f ms\n",
        static_cast<unsigned long long>(pushed),
        static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(got),
        static_cast<unsigned long long>(dropped), ms);

    CHECK(total == pushed);
    CHECK(got + dropped == total); // every record either received or dropped
    CHECK(got > 0);
    CHECK(overMax.load() == 0);    // no over-max drain, no torn/reordered record
    CHECK(ring.Drain(nullptr, 0) == 0);
}

// ---------------------------------------------------------------------------
// (a2) Zero-drop variant: paced producers so nothing is dropped. Then EVERY
// pushed record must be received, exactly once, in strictly increasing seq
// order. This is the direct "consumer receives every non-dropped record" proof.
// ---------------------------------------------------------------------------
void TestNoDropOrdering()
{
    constexpr int kPerProducerPaced = 5000;
    constexpr size_t kTotalRecords = static_cast<size_t>(kProducers) * kPerProducerPaced;
    // Sized to hold the whole run, so no record can be dropped regardless of
    // scheduling: this test isolates ordering and completeness from backpressure.
    LogRing ring(12u << 20);
    CHECK(ring.SlotCount() >= kTotalRecords);
    constexpr size_t kMaxPerDrain = 64;

    std::atomic<bool> producersDone{0};
    std::vector<uint64_t> got;
    got.reserve(static_cast<size_t>(kProducers) * kPerProducerPaced);
    Validation v(kProducers);

    std::thread consumer([&]
    {
        std::vector<LogRecord> buf(kMaxPerDrain);
        for (;;)
        {
            const size_t n = ring.Drain(buf.data(), kMaxPerDrain);
            if (n == 0)
            {
                if (producersDone.load(std::memory_order_acquire) &&
                    ring.Drain(buf.data(), kMaxPerDrain) == 0)
                    break;
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < n; ++i)
            {
                v.Check(buf[i]);
                got.push_back(buf[i].seq);
            }
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&ring, p]
        {
            char buf[128];
            for (int i = 0; i < kPerProducerPaced; ++i)
            {
                const int n = std::snprintf(buf, sizeof(buf), "P%d-%d-%s", p, i, kFiller);
                // The ring holds the entire run, so this must always succeed.
                CHECK(ring.Push(LogLevel::Info, static_cast<uint32_t>(p),
                    static_cast<uint64_t>(i), buf, static_cast<size_t>(n)));
            }
        });
    }

    for (std::thread& t : producers)
        t.join();
    producersDone.store(true, std::memory_order_release);
    consumer.join();

    const uint64_t pushed = static_cast<uint64_t>(kProducers) * kPerProducerPaced;
    std::printf("[a2] pushed=%llu received=%zu dropped=%llu slots=%zu\n",
        static_cast<unsigned long long>(pushed), got.size(),
        static_cast<unsigned long long>(ring.DroppedCount()), ring.SlotCount());

    CHECK(ring.DroppedCount() == 0);      // ring held everything: nothing dropped
    CHECK(got.size() == pushed);          // every pushed record received
    CHECK(v.badSeq == 0 && v.badText == 0 && v.badOrder == 0);

    // Globally strictly increasing by exactly one: complete and in order.
    for (size_t i = 1; i < got.size(); ++i)
        CHECK(got[i] == got[i - 1] + 1);
}

// ---------------------------------------------------------------------------
// (b) A payload larger than the ring is dropped, not crashed.
// ---------------------------------------------------------------------------
void TestOversizePayload()
{
    LogRing ring(1024);
    CHECK(ring.SlotCount() >= 4);

    std::string huge(100000, 'H');
    CHECK(!ring.Push(LogLevel::Error, 1, 0, huge.data(), huge.size()));
    CHECK(ring.DroppedCount() == 1);
    CHECK(ring.TotalCount() == 1);

    std::string overCap(LogRing::kPayloadBytes + 1, 'O');
    CHECK(!ring.Push(LogLevel::Error, 1, 0, overCap.data(), overCap.size()));
    CHECK(ring.DroppedCount() == 2);

    CHECK(!ring.Push(LogLevel::Info, 1, 0, nullptr, 4)); // null payload, non-zero len
    CHECK(ring.DroppedCount() == 3);

    const char* ok = "still alive";
    CHECK(ring.Push(LogLevel::Info, 2, 7, ok, std::strlen(ok)));

    LogRecord out[4];
    CHECK(ring.Drain(out, 4) == 1);
    CHECK(std::strcmp(out[0].text, ok) == 0);
    CHECK(out[0].len == std::strlen(ok));
    CHECK(ring.DroppedCount() == 3);
    std::printf("[b] oversize payloads dropped cleanly, ring still usable\n");
}

// ---------------------------------------------------------------------------
// (c) Drain respects maxRecords and returns records in order.
// ---------------------------------------------------------------------------
void TestDrainBatching()
{
    LogRing ring(1u << 20);
    constexpr int kCount = 100;
    char buf[64];
    for (int i = 0; i < kCount; ++i)
    {
        const int n = std::snprintf(buf, sizeof(buf), "rec-%d", i);
        CHECK(ring.Push(LogLevel::Debug, 9, static_cast<uint64_t>(i), buf, static_cast<size_t>(n)));
    }

    LogRecord out[8];
    int seen = 0;
    uint64_t lastSeq = 0;
    bool haveSeq = false;

    for (;;)
    {
        const size_t n = ring.Drain(out, 7);
        CHECK(n <= 7);
        if (n == 0)
            break;
        for (size_t i = 0; i < n; ++i)
        {
            char expected[64];
            std::snprintf(expected, sizeof(expected), "rec-%d", seen);
            CHECK(std::strcmp(out[i].text, expected) == 0);
            CHECK(out[i].timestampMs == static_cast<uint64_t>(seen));
            CHECK(out[i].threadId == 9);
            CHECK(out[i].level == LogLevel::Debug);
            if (haveSeq)
                CHECK(out[i].seq == lastSeq + 1);
            lastSeq = out[i].seq;
            haveSeq = true;
            ++seen;
        }
    }

    CHECK(seen == kCount);
    CHECK(ring.DroppedCount() == 0);
    CHECK(ring.Drain(out, 7) == 0);
    std::printf("[c] maxRecords honoured, %d records in order across batched drains\n", seen);
}

// A full ring drops instead of blocking or growing.
void TestFullRingDrops()
{
    LogRing ring(1024);
    const size_t slots = ring.SlotCount();
    size_t accepted = 0;
    for (size_t i = 0; i < slots * 4; ++i)
    {
        if (ring.Push(LogLevel::Info, 0, i, "x", 1))
            ++accepted;
    }
    CHECK(accepted <= slots);
    CHECK(ring.DroppedCount() == slots * 4 - accepted);
    CHECK(ring.TotalCount() == slots * 4);
    std::printf("[d] full ring: accepted=%zu slots=%zu dropped=%llu (no block, no growth)\n",
        accepted, slots, static_cast<unsigned long long>(ring.DroppedCount()));
}


// ---------------------------------------------------------------------------
// (e) REGRESSION GUARD for the scratch-arena overflow.
//
// Drain() hands each slot back to the producers as soon as it is copied out, so
// producers can refill slots WHILE the drain loop is still running and the loop
// can observe more records than there are slots. The consumer-owned scratch
// arena holds exactly m_slotCount records, so an unbounded loop writes past its
// end (heap buffer overflow / access violation).
//
// Guard: one Drain() call must never return more than SlotCount() records, and
// the bytes it copies into the arena in that call must never exceed the arena.
// ---------------------------------------------------------------------------
void TestScratchArenaBound()
{
    constexpr size_t kCapacityBytes = 4096;
    constexpr size_t kMaxDrain = 200000; // deliberately far larger than SlotCount()
    // Near-maximum payload: maximizes bytes copied per record, so an over-drain
    // runs past the arena end rather than merely being detectable.
    const std::string payload(LogRing::kPayloadBytes - 8, 'A');

    LogRing ring(kCapacityBytes);
    const size_t slots = ring.SlotCount();
    const size_t arenaBytes = slots * (LogRing::kPayloadBytes + 1);
    std::printf("[e] regression: capacity=%zu -> slots=%zu arena=%zu bytes, Drain(max=%zu)\n",
        kCapacityBytes, slots, arenaBytes, kMaxDrain);
    CHECK(slots < kMaxDrain);

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> pushed{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < 4; ++p)
    {
        producers.emplace_back([&ring, &stop, &pushed, &payload]
        {
            while (!stop.load(std::memory_order_relaxed))
            {
                ring.Push(LogLevel::Info, 1, 0, payload.data(), payload.size());
                pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<LogRecord> buf(kMaxDrain);
    size_t maxBatch = 0;
    size_t maxBytes = 0;
    uint64_t totalRecords = 0;

    for (int iter = 0; iter < 20000; ++iter)
    {
        const size_t n = ring.Drain(buf.data(), kMaxDrain);

        // THE INVARIANT: one Drain can never exceed the slot count, because the
        // arena holds exactly SlotCount() records.
        CHECK(n <= slots);

        if (n > maxBatch)
            maxBatch = n;

        size_t bytes = 0;
        for (size_t i = 0; i < n; ++i)
        {
            CHECK(buf[i].len <= LogRing::kPayloadBytes);
            CHECK(std::strlen(buf[i].text) == buf[i].len);
            bytes += static_cast<size_t>(buf[i].len) + 1;
        }
        CHECK(bytes <= arenaBytes);
        if (bytes > maxBytes)
            maxBytes = bytes;

        totalRecords += n;
    }

    stop.store(true, std::memory_order_release);
    for (std::thread& t : producers)
        t.join();

    std::printf("[e] regression: pushed=%llu drained=%llu maxBatch=%zu (limit %zu) maxBytes=%zu (limit %zu)\n",
        static_cast<unsigned long long>(pushed.load()),
        static_cast<unsigned long long>(totalRecords),
        maxBatch, slots, maxBytes, arenaBytes);

    CHECK(maxBatch <= slots);
    CHECK(maxBytes <= arenaBytes);
    CHECK(totalRecords > 0);
}

} // namespace

int main()
{
    TestConcurrent();
    TestNoDropOrdering();
    TestOversizePayload();
    TestDrainBatching();
    TestFullRingDrops();
    TestScratchArenaBound();
    std::printf("ALL LOG RING TESTS PASSED\n");
    return 0;
}
