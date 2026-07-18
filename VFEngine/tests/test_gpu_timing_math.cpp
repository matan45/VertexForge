#include <doctest.h>
#include <stats/GpuTimingMath.hpp>
#include <vector>

// ============================================================
// GpuTimingMath: the boundary-chain arithmetic and name-keyed
// EMA backing the profiler's per-pass GPU timings (VK-1529).
// ============================================================

using namespace render::timing;

TEST_SUITE("GpuTimingMath")
{

TEST_CASE("boundaryDeltaMs converts ticks to milliseconds")
{
    // 1'000'000 ticks at 1ns each == 1ms
    CHECK(boundaryDeltaMs(0, 1'000'000, 1.0, 64) == doctest::Approx(1.0f));
    // A non-unit period scales linearly
    CHECK(boundaryDeltaMs(0, 1'000'000, 2.5, 64) == doctest::Approx(2.5f));
    // Offsets cancel — only the delta matters
    CHECK(boundaryDeltaMs(500'000, 1'500'000, 1.0, 64) == doctest::Approx(1.0f));
    CHECK(boundaryDeltaMs(42, 42, 1.0, 64) == doctest::Approx(0.0f));
}

TEST_CASE("boundaryDeltaMs rejects degenerate inputs instead of dividing by them")
{
    CHECK(boundaryDeltaMs(0, 1'000'000, 0.0, 64) == 0.0f);
    CHECK(boundaryDeltaMs(0, 1'000'000, -1.0, 64) == 0.0f);
    // validBits == 0 means the queue cannot timestamp at all
    CHECK(boundaryDeltaMs(0, 1'000'000, 1.0, 0) == 0.0f);
}

TEST_CASE("boundaryDeltaMs handles counter wrap at the valid width")
{
    // The spec allows as few as 36 valid bits; at ~1ns that wraps every ~69s.
    // An interval straddling the wrap must report its real duration -- the old
    // `end < start -> 0.0f` guard silently reported such a pass as free.
    constexpr uint64_t kWidth36 = uint64_t{1} << 36;

    const uint64_t start = kWidth36 - 1'000'000;
    const uint64_t end = 2'000'000; // wrapped past zero
    CHECK(boundaryDeltaMs(start, end, 1.0, 36) == doctest::Approx(3.0f));
}

TEST_CASE("boundaryDeltaMs clamps an inverted pair to zero")
{
    // A modular delta in the upper half of the range is not a very long wrap --
    // a real interval is at most a frame -- it is an inverted pair. Reporting
    // ~2^validBits ticks would pin the EMA for seconds and flatten the history
    // plot's whole window, so it must read 0 instead.
    CHECK(boundaryDeltaMs(5'000'000, 1'000'000, 1.0, 64) == 0.0f);

    // The same raw values that are a legitimate wrap at 36 bits are an inversion
    // at 64, where the counter has not wrapped at all.
    constexpr uint64_t kWidth36 = uint64_t{1} << 36;
    const uint64_t start = kWidth36 - 1'000'000;
    const uint64_t end = 2'000'000;
    CHECK(boundaryDeltaMs(start, end, 1.0, 36) == doctest::Approx(3.0f));
    CHECK(boundaryDeltaMs(start, end, 1.0, 64) == 0.0f);

    // A delta sitting exactly on the half-range ceiling is still accepted --
    // the guard rejects strictly above it.
    CHECK(boundaryDeltaMs(0, (kWidth36 >> 1) - 1, 1.0, 36) > 0.0f);
}

TEST_CASE("passDeltasFromBoundaries partitions the frame exactly")
{
    // The whole point of the chain: parts sum to the total, by construction.
    // Two passes, true frame 10ms.
    std::vector<uint64_t> boundaries{0, 6'000'000, 10'000'000};
    std::vector<float> ms;
    float total = 0.0f;

    passDeltasFromBoundaries(boundaries, 1.0, 64, ms, total);

    REQUIRE(ms.size() == 2);
    CHECK(ms[0] == doctest::Approx(6.0f));
    CHECK(ms[1] == doctest::Approx(4.0f));
    CHECK(total == doctest::Approx(10.0f));
    CHECK(ms[0] + ms[1] == doctest::Approx(total));
}

TEST_CASE("passDeltasFromBoundaries shares never exceed the total")
{
    // Regression guard for the overcounting the chain replaces: with paired
    // top-of-pipe/bottom-of-pipe timestamps each pass re-counted its
    // predecessors' drain, so the sum could exceed the real frame time.
    std::vector<uint64_t> boundaries{0, 1'000'000, 3'000'000, 4'000'000, 10'000'000};
    std::vector<float> ms;
    float total = 0.0f;

    passDeltasFromBoundaries(boundaries, 1.0, 64, ms, total);

    REQUIRE(ms.size() == 4);
    float sum = 0.0f;
    for (float v : ms)
    {
        CHECK(v >= 0.0f);
        CHECK(v <= total);
        sum += v;
    }
    CHECK(sum == doctest::Approx(total));
}

TEST_CASE("passDeltasFromBoundaries needs at least one interval")
{
    std::vector<float> ms{1.0f, 2.0f}; // must be cleared even on the early-out
    float total = 99.0f;

    passDeltasFromBoundaries({}, 1.0, 64, ms, total);
    CHECK(ms.empty());
    CHECK(total == 0.0f);

    passDeltasFromBoundaries({42}, 1.0, 64, ms, total);
    CHECK(ms.empty());
    CHECK(total == 0.0f);
}

TEST_CASE("NamedEma seeds each entry from its own first sample")
{
    NamedEma ema;
    // Seeded exactly -- NOT 0.1 * 10 climbing out of zero.
    CHECK(ema.update("SceneMeshes", 10.0f, 0) == doctest::Approx(10.0f));
    // alpha 0.1: 0.1*20 + 0.9*10
    CHECK(ema.update("SceneMeshes", 20.0f, 1) == doctest::Approx(11.0f));
}

TEST_CASE("NamedEma keeps passes independent when the pass set changes")
{
    // The positional-index bug this replaces: toggling SSR shifted every later
    // index, blending one pass's history into another's samples.
    NamedEma ema;
    ema.update("SSR", 5.0f, 0);
    ema.update("SSGI", 100.0f, 1); // a very different neighbour appears

    // SSR is unmoved by SSGI's samples.
    CHECK(ema.update("SSR", 5.0f, 2) == doctest::Approx(5.0f));
    CHECK(ema.size() == 2);
}

TEST_CASE("NamedEma prunes entries unseen for maxAge frames")
{
    NamedEma ema;
    ema.update("Clouds", 3.0f, 1);
    ema.update("Upscale", 4.0f, 50);

    ema.prune(55, 10); // Clouds last seen at 1, so 55 > 1 + 10
    CHECK(ema.size() == 1);
    // Upscale survives and keeps its history rather than re-seeding.
    CHECK(ema.update("Upscale", 4.0f, 56) == doctest::Approx(4.0f));

    ema.prune(200, 10);
    CHECK(ema.size() == 0);
}

TEST_CASE("NamedEma prune does not underflow on young entries")
{
    // frame - lastSeen would wrap for a uint64 if written naively.
    NamedEma ema;
    ema.update("Atmosphere", 1.0f, 0);
    ema.prune(0, 600);
    CHECK(ema.size() == 1);
}

TEST_CASE("NamedEma clear drops all history")
{
    NamedEma ema;
    ema.update("IBL", 7.0f, 0);
    ema.clear();
    CHECK(ema.size() == 0);
    // Re-seeds exactly rather than blending with the cleared value.
    CHECK(ema.update("IBL", 2.0f, 1) == doctest::Approx(2.0f));
}

}
