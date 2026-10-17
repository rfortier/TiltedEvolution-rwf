// sparkline_test.cpp - acceptance tests for serverui::Sparkline.
// Plain CHECK macro, no gtest. Returns non-zero on the first failure.

#include "../src/ui/Sparkline.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

bool NearlyEqual(double a, double b)
{
    return std::fabs(a - b) < 1e-9;
}

// ---------------------------------------------------------------------------
// (a) ring wrap-around: the held window is always the newest capacity-many
//     samples, in oldest-to-newest order, however far the series runs.
// ---------------------------------------------------------------------------
void TestWrapAround()
{
    Sparkline series(4);
    CHECK(series.Capacity() == 4);
    CHECK(series.Size() == 0);
    CHECK(series.Empty());

    // Fill without wrapping.
    series.Push(1.0);
    series.Push(2.0);
    series.Push(3.0);
    series.Push(4.0);
    CHECK(series.Size() == 4);
    CHECK(!series.Empty());
    for (size_t i = 0; i < 4; ++i)
        CHECK(series.At(i) == static_cast<double>(i + 1));
    CHECK(series.Last() == 4.0);

    // One past capacity: 1.0 is evicted, the rest shift down.
    series.Push(5.0);
    CHECK(series.Size() == 4);
    CHECK(series.At(0) == 2.0);
    CHECK(series.At(1) == 3.0);
    CHECK(series.At(2) == 4.0);
    CHECK(series.At(3) == 5.0);
    CHECK(series.Last() == 5.0);

    // Wrap several times over, and check the whole window each push.
    Sparkline ring(5);
    for (int i = 0; i < 100; ++i)
    {
        ring.Push(static_cast<double>(i));
        const size_t expected = static_cast<size_t>(i + 1) < 5 ? static_cast<size_t>(i + 1) : 5;
        CHECK(ring.Size() == expected);
        for (size_t k = 0; k < ring.Size(); ++k)
            CHECK(ring.At(k) == static_cast<double>(i + 1 - ring.Size() + k));
        CHECK(ring.Last() == static_cast<double>(i));
    }

    // Out-of-range access is 0.0, not a crash or a stale sample.
    CHECK(ring.At(5) == 0.0);
    CHECK(ring.At(1000) == 0.0);

    // Capacity 1 is legal and always holds the newest sample.
    Sparkline one(1);
    one.Push(7.0);
    CHECK(one.Size() == 1);
    one.Push(8.0);
    CHECK(one.Size() == 1);
    CHECK(one.At(0) == 8.0);
    CHECK(one.Min() == 8.0);
    CHECK(one.Max() == 8.0);
    CHECK(one.Avg() == 8.0);

    // Capacity 0 is clamped to 1 rather than dividing by zero.
    Sparkline zero(0);
    CHECK(zero.Capacity() == 1);
    zero.Push(3.0);
    CHECK(zero.Size() == 1);
    CHECK(zero.At(0) == 3.0);

    // Clear resets the ring and the aggregates.
    ring.Clear();
    CHECK(ring.Size() == 0);
    CHECK(ring.Empty());
    CHECK(ring.Last() == 0.0);
    CHECK(ring.Min() == 0.0);
    CHECK(ring.Max() == 0.0);
    CHECK(ring.Avg() == 0.0);
    ring.Push(2.0);
    CHECK(ring.Size() == 1);
    CHECK(ring.At(0) == 2.0);

    std::printf("[a] ring wrap-around holds the newest window in order\n");
}

// ---------------------------------------------------------------------------
// (b) min / max / avg over the held window only, recomputed after eviction.
// ---------------------------------------------------------------------------
void TestAggregates()
{
    Sparkline empty(8);
    CHECK(empty.Min() == 0.0);
    CHECK(empty.Max() == 0.0);
    CHECK(empty.Avg() == 0.0);

    Sparkline series(4);
    series.Push(10.0);
    CHECK(series.Min() == 10.0);
    CHECK(series.Max() == 10.0);
    CHECK(NearlyEqual(series.Avg(), 10.0));

    series.Push(20.0);
    series.Push(30.0);
    CHECK(series.Min() == 10.0);
    CHECK(series.Max() == 30.0);
    CHECK(NearlyEqual(series.Avg(), 20.0));

    // A negative sample is a legal reading.
    series.Push(-5.0);
    CHECK(series.Min() == -5.0);
    CHECK(series.Max() == 30.0);
    CHECK(NearlyEqual(series.Avg(), 13.75));

    // Eviction of the extreme must recompute, not keep a stale min/max.
    series.Push(100.0); // evicts 10.0
    CHECK(series.Size() == 4);
    CHECK(series.At(0) == 20.0);
    CHECK(series.Min() == -5.0);
    CHECK(series.Max() == 100.0);
    CHECK(NearlyEqual(series.Avg(), 36.25));

    // Push(1.0) evicts 20.0: window is [30, -5, 100, 1], the extreme survives.
    series.Push(1.0);
    CHECK(series.At(0) == 30.0);
    CHECK(series.Min() == -5.0);
    CHECK(series.Max() == 100.0);
    CHECK(NearlyEqual(series.Avg(), 31.5));

    // Push(2.0) evicts 30.0: window is [-5, 100, 1, 2].
    series.Push(2.0);
    CHECK(series.Min() == -5.0);

    // Push(3.0) finally evicts the -5.0, and the minimum must recompute.
    series.Push(3.0);
    CHECK(series.At(0) == 100.0);
    CHECK(series.Min() == 1.0);
    CHECK(series.Max() == 100.0);
    CHECK(NearlyEqual(series.Avg(), 26.5));

    // Longer deterministic sweep against a reference window.
    Sparkline sweep(7);
    std::vector<double> reference;
    for (int i = 0; i < 200; ++i)
    {
        const double value = static_cast<double>((i * 37) % 101) - 50.0;
        sweep.Push(value);
        reference.push_back(value);
        if (reference.size() > 7)
            reference.erase(reference.begin());

        double lo = reference[0];
        double hi = reference[0];
        double sum = 0.0;
        for (double v : reference)
        {
            if (v < lo)
                lo = v;
            if (v > hi)
                hi = v;
            sum += v;
        }
        CHECK(NearlyEqual(sweep.Min(), lo));
        CHECK(NearlyEqual(sweep.Max(), hi));
        CHECK(NearlyEqual(sweep.Avg(), sum / static_cast<double>(reference.size())));
    }

    std::printf("[b] min/max/avg exact over the held window, incl. after eviction\n");
}

// ---------------------------------------------------------------------------
// (c) normalisation for drawing.
// ---------------------------------------------------------------------------
void TestNormalization()
{
    Sparkline series(4);
    series.Push(10.0);
    series.Push(20.0);
    series.Push(30.0);

    CHECK(series.NormalizedAt(0) == 0.0); // the minimum
    CHECK(NearlyEqual(series.NormalizedAt(1), 0.5));
    CHECK(series.NormalizedAt(2) == 1.0); // the maximum
    CHECK(series.NormalizedAt(3) == 0.0); // out of range

    // Explicit range, with clamping outside it.
    CHECK(series.NormalizedAt(0, 0.0, 100.0) == 0.1);
    CHECK(NearlyEqual(series.NormalizedAt(2, 0.0, 40.0), 0.75));
    CHECK(series.NormalizedAt(0, 15.0, 25.0) == 0.0); // below the range
    CHECK(series.NormalizedAt(2, 15.0, 25.0) == 1.0); // above the range

    // Inverted range is swapped, not divided by a negative span.
    CHECK(series.NormalizedAt(0, 30.0, 10.0) == 0.0);
    CHECK(series.NormalizedAt(2, 30.0, 10.0) == 1.0);

    // A flat series renders as a midline instead of collapsing to the edge.
    Sparkline flat(4);
    flat.Push(5.0);
    flat.Push(5.0);
    flat.Push(5.0);
    CHECK(flat.NormalizedAt(0) == 0.5);
    CHECK(flat.NormalizedAt(1, 5.0, 5.0) == 0.5);
    CHECK(flat.NormalizedAt(2, 9.0, 9.0) == 0.5);

    // A single sample is also flat.
    Sparkline single(4);
    single.Push(42.0);
    CHECK(single.NormalizedAt(0) == 0.5);

    // BuildNormalized: one point per sample when the buffer is big enough.
    double points[8];
    const size_t written = series.BuildNormalized(points, 8);
    CHECK(written == 3);
    CHECK(points[0] == 0.0);
    CHECK(NearlyEqual(points[1], 0.5));
    CHECK(points[2] == 1.0);

    // A smaller buffer samples evenly and always ends on the newest sample.
    double small[2];
    CHECK(series.BuildNormalized(small, 2) == 2);
    CHECK(small[0] == 0.0);
    CHECK(small[1] == 1.0);

    Sparkline longSeries(16);
    for (int i = 0; i <= 10; ++i)
        longSeries.Push(static_cast<double>(i));
    double mid[3];
    CHECK(longSeries.BuildNormalized(mid, 3) == 3);
    CHECK(mid[0] == 0.0);            // value 0 is the min
    CHECK(NearlyEqual(mid[1], 0.5)); // value 5
    CHECK(mid[2] == 1.0);            // value 10 is the newest and the max

    // Degenerate calls write nothing.
    CHECK(series.BuildNormalized(nullptr, 8) == 0);
    CHECK(series.BuildNormalized(points, 0) == 0);
    Sparkline none(4);
    CHECK(none.BuildNormalized(points, 8) == 0);

    // Buffer larger than the series fills only the series' length.
    double big[16];
    CHECK(flat.BuildNormalized(big, 16) == 3);

    std::printf("[c] normalisation and BuildNormalized exact\n");
}

} // namespace

int main()
{
    TestWrapAround();
    TestAggregates();
    TestNormalization();
    std::printf("ALL SPARKLINE TESTS PASSED\n");
    return 0;
}
