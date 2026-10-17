#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace serverui
{

enum class LogLevel { Trace, Debug, Info, Warn, Error, Critical };

struct LogRecord // POD, trivially copyable
{
    uint64_t seq;        // strictly increasing; drops are reported by DroppedCount()
    LogLevel level;
    uint64_t timestampMs; // steady_clock epoch ms
    uint32_t threadId;
    uint32_t len;         // payload bytes
    const char* text;     // valid ONLY until the next Drain(); points into ring arena
};

// Multi-producer / single-consumer byte-bounded log ring.
//
// Storage is a ring of fixed slots reserved by a CAS on a monotonic ticket
// counter (bounded-queue scheme). The consumer only ever hands out records in
// ticket order, so Drain() is always in sequence order.
//
// Push() never blocks, never allocates and never throws. A record that does not
// fit (ring full, or payload above kPayloadBytes) is dropped and counted.
//
// NOTE ON seq: seq is the record's ticket, i.e. its position in the delivered
// stream, so it is strictly increasing and contiguous. A full ring cannot
// consume a ticket for a record it cannot store, so drops are not observable as
// seq gaps; they are reported by DroppedCount() and TotalCount() instead.
class LogRing
{
public:
    // Largest payload a single record may carry. Records above this are dropped.
    static constexpr size_t kPayloadBytes = 464;

    explicit LogRing(size_t aCapacityBytes);
    ~LogRing();

    LogRing(const LogRing&) = delete;
    LogRing& operator=(const LogRing&) = delete;

    // Producer side. false == record dropped.
    bool Push(LogLevel aLevel, uint32_t aThreadId, uint64_t aTimestampMs, const char* acData, size_t aLen);

    // Consumer side (UI thread only). Fills apOut with at most aMaxRecords.
    // Returns at most min(aMaxRecords, SlotCount()) records, because the scratch
    // arena backing LogRecord::text holds exactly SlotCount() records. Call again
    // to drain the remainder.
    size_t Drain(LogRecord* apOut, size_t aMaxRecords);

    uint64_t DroppedCount() const;
    uint64_t TotalCount() const;
    size_t SlotCount() const { return m_slotCount; }

private:
    struct Slot
    {
        std::atomic<uint64_t> state; // ready protocol: == ticket when writable
        uint64_t timestampMs;
        uint32_t len;
        uint32_t threadId;
        LogLevel level;
        char payload[kPayloadBytes];
    };

    Slot* m_slots;
    size_t m_slotCount;
    char* m_scratch;      // consumer-owned copy arena, sized once
    size_t m_scratchBytes;
    std::atomic<uint64_t> m_enqueue;
    std::atomic<uint64_t> m_dequeue;
    std::atomic<uint64_t> m_dropped;
    std::atomic<uint64_t> m_total;
};

} // namespace serverui
