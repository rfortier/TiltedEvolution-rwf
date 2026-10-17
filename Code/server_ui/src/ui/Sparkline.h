#pragma once

// Sparkline: a fixed-capacity rolling series of samples plus the small amount
// of arithmetic a metrics curve needs (min / max / avg / normalised points).
//
// No ImGui, no allocation after construction, no window of "the last N ms":
// the caller decides when to push. Capacity is fixed at construction and the
// buffer is a ring, so pushing forever costs nothing and never grows.

#include <cstddef>
#include <vector>

namespace serverui
{

class Sparkline
{
public:
    // aCapacity is clamped to at least 1.
    explicit Sparkline(size_t aCapacity = 120);

    // Newest sample. Once the ring is full the oldest is overwritten.
    void Push(double aValue);

    void Clear();

    size_t Capacity() const { return m_capacity; }
    size_t Size() const { return m_count; }
    bool Empty() const { return m_count == 0; }

    // 0 == oldest still-held sample, Size()-1 == newest. Out of range is 0.0.
    double At(size_t aIndex) const;
    double Last() const;

    // Aggregates over the held samples. An empty series reports 0.0 for all
    // three: there is nothing to average, and 0 is what the UI draws anyway.
    double Min() const;
    double Max() const;
    double Avg() const;

    // Value mapped into [0, 1] against the series' own min/max. A flat series
    // maps to 0.5 so the curve renders as a straight midline instead of
    // collapsing onto the bottom edge.
    double NormalizedAt(size_t aIndex) const;

    // Same, against a caller-supplied range. aMin > aMax is swapped; aMin ==
    // aMax still yields 0.5.
    double NormalizedAt(size_t aIndex, double aMin, double aMax) const;

    // Fills apOut with up to aMaxPoints normalised samples, oldest first,
    // sampling evenly across the series when it is longer than the buffer.
    // Returns the number of doubles written.
    size_t BuildNormalized(double* apOut, size_t aMaxPoints) const;

private:
    size_t IndexOf(size_t aIndex) const { return (m_head + aIndex) % m_capacity; }

    std::vector<double> m_samples;
    size_t m_capacity;
    size_t m_head{0}; // slot of the oldest sample once the ring has wrapped
    size_t m_count{0};
};

} // namespace serverui
