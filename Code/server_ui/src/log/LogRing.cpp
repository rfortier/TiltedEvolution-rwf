#include "LogRing.h"

#include <algorithm>
#include <cstring>

namespace serverui
{

LogRing::LogRing(size_t aCapacityBytes)
    : m_slots(nullptr)
    , m_slotCount(0)
    , m_scratch(nullptr)
    , m_scratchBytes(0)
    , m_enqueue(0)
    , m_dequeue(0)
    , m_dropped(0)
    , m_total(0)
{
    // Allocated once, here. Nothing on the producer or consumer path allocates.
    m_slotCount = std::max<size_t>(aCapacityBytes / sizeof(Slot), 4);
    m_slots = new Slot[m_slotCount];

    for (size_t i = 0; i < m_slotCount; ++i)
    {
        Slot& slot = m_slots[i];
        slot.state.store(static_cast<uint64_t>(i), std::memory_order_relaxed);
        slot.timestampMs = 0;
        slot.len = 0;
        slot.threadId = 0;
        slot.level = LogLevel::Info;
        std::memset(slot.payload, 0, sizeof(slot.payload));
    }

    m_scratchBytes = m_slotCount * (kPayloadBytes + 1);
    m_scratch = new char[m_scratchBytes]();
}

LogRing::~LogRing()
{
    delete[] m_slots;
    delete[] m_scratch;
}

bool LogRing::Push(LogLevel aLevel, uint32_t aThreadId, uint64_t aTimestampMs, const char* acData, size_t aLen)
{
    m_total.fetch_add(1, std::memory_order_relaxed);

    if (aLen > kPayloadBytes || (aLen != 0 && acData == nullptr))
    {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    uint64_t pos = m_enqueue.load(std::memory_order_relaxed);
    Slot* pSlot = nullptr;

    for (;;)
    {
        pSlot = &m_slots[pos % m_slotCount];
        const uint64_t state = pSlot->state.load(std::memory_order_acquire);
        const int64_t diff = static_cast<int64_t>(state) - static_cast<int64_t>(pos);

        if (diff == 0)
        {
            if (m_enqueue.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                break;
            continue; // pos was reloaded by the failed CAS
        }

        if (diff < 0)
        {
            // Slot still owned by an unread record: ring is full. Drop, never spin.
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        pos = m_enqueue.load(std::memory_order_relaxed);
    }

    // This slot is exclusively ours until the release store below publishes it.
    pSlot->timestampMs = aTimestampMs;
    pSlot->len = static_cast<uint32_t>(aLen);
    pSlot->threadId = aThreadId;
    pSlot->level = aLevel;
    if (aLen != 0)
        std::memcpy(pSlot->payload, acData, aLen);

    pSlot->state.store(pos + 1, std::memory_order_release);
    return true;
}

size_t LogRing::Drain(LogRecord* apOut, size_t aMaxRecords)
{
    if (apOut == nullptr || aMaxRecords == 0)
        return 0;

    // The scratch arena holds exactly m_slotCount records, and slots are handed
    // back to the producers as they are copied out, so the loop could otherwise
    // recycle slots and run past the arena end. Bound the copy count by the slot
    // count: the arena is then sufficient by construction.
    const size_t cap = std::min(aMaxRecords, m_slotCount);

    size_t count = 0;
    size_t scratchUsed = 0;
    uint64_t pos = m_dequeue.load(std::memory_order_relaxed);

    while (count < cap)
    {
        Slot* pSlot = &m_slots[pos % m_slotCount];
        if (pSlot->state.load(std::memory_order_acquire) != pos + 1)
            break; // empty, or the owning producer has not published yet

        const size_t len = pSlot->len;
        char* pDst = m_scratch + scratchUsed;
        if (len != 0)
            std::memcpy(pDst, pSlot->payload, len);
        pDst[len] = '\0';
        scratchUsed += len + 1;

        apOut[count].seq = pos;
        apOut[count].level = pSlot->level;
        apOut[count].timestampMs = pSlot->timestampMs;
        apOut[count].threadId = pSlot->threadId;
        apOut[count].len = static_cast<uint32_t>(len);
        apOut[count].text = pDst;

        // Hand the slot back to the producers immediately.
        pSlot->state.store(pos + m_slotCount, std::memory_order_release);
        ++pos;
        ++count;
    }

    m_dequeue.store(pos, std::memory_order_relaxed);
    return count;
}

uint64_t LogRing::DroppedCount() const
{
    return m_dropped.load(std::memory_order_relaxed);
}

uint64_t LogRing::TotalCount() const
{
    return m_total.load(std::memory_order_relaxed);
}

} // namespace serverui
