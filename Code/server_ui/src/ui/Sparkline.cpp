#include "Sparkline.h"

#include <algorithm>

namespace serverui
{

Sparkline::Sparkline(size_t aCapacity)
    : m_capacity(aCapacity < 1 ? 1 : aCapacity)
{
    m_samples.resize(m_capacity, 0.0);
}

void Sparkline::Push(double aValue)
{
    if (m_count < m_capacity)
    {
        m_samples[(m_head + m_count) % m_capacity] = aValue;
        ++m_count;
        return;
    }

    // Full: overwrite the oldest and advance the head, so the ring wraps
    // without ever moving more than one element.
    m_samples[m_head] = aValue;
    m_head = (m_head + 1) % m_capacity;
}

void Sparkline::Clear()
{
    m_head = 0;
    m_count = 0;
    std::fill(m_samples.begin(), m_samples.end(), 0.0);
}

double Sparkline::At(size_t aIndex) const
{
    if (aIndex >= m_count)
        return 0.0;
    return m_samples[IndexOf(aIndex)];
}

double Sparkline::Last() const
{
    if (m_count == 0)
        return 0.0;
    return m_samples[IndexOf(m_count - 1)];
}

double Sparkline::Min() const
{
    if (m_count == 0)
        return 0.0;
    double result = m_samples[IndexOf(0)];
    for (size_t i = 1; i < m_count; ++i)
        result = std::min(result, m_samples[IndexOf(i)]);
    return result;
}

double Sparkline::Max() const
{
    if (m_count == 0)
        return 0.0;
    double result = m_samples[IndexOf(0)];
    for (size_t i = 1; i < m_count; ++i)
        result = std::max(result, m_samples[IndexOf(i)]);
    return result;
}

double Sparkline::Avg() const
{
    if (m_count == 0)
        return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < m_count; ++i)
        sum += m_samples[IndexOf(i)];
    return sum / static_cast<double>(m_count);
}

double Sparkline::NormalizedAt(size_t aIndex) const
{
    return NormalizedAt(aIndex, Min(), Max());
}

double Sparkline::NormalizedAt(size_t aIndex, double aMin, double aMax) const
{
    if (aIndex >= m_count)
        return 0.0;

    if (aMin > aMax)
        std::swap(aMin, aMax);

    const double span = aMax - aMin;
    if (span <= 0.0)
        return 0.5;

    const double value = m_samples[IndexOf(aIndex)];
    double normalized = (value - aMin) / span;
    if (normalized < 0.0)
        normalized = 0.0;
    if (normalized > 1.0)
        normalized = 1.0;
    return normalized;
}

size_t Sparkline::BuildNormalized(double* apOut, size_t aMaxPoints) const
{
    if (apOut == nullptr || aMaxPoints == 0 || m_count == 0)
        return 0;

    const double lo = Min();
    const double hi = Max();
    const size_t points = std::min(aMaxPoints, m_count);

    // Even stride across the series, always including the newest sample so the
    // right edge of the curve is the current reading.
    for (size_t i = 0; i < points; ++i)
    {
        const size_t source = (points == 1)
            ? m_count - 1
            : (m_count - 1) * i / (points - 1);
        apOut[i] = NormalizedAt(source, lo, hi);
    }
    return points;
}

} // namespace serverui
