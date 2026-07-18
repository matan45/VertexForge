#include <doctest.h>
#include <stats/GpuPassStats.hpp>
#include <vector>

// ============================================================
// GpuPassStats: the two-tier sink carrying GPU timings from the
// graphics module to the editor profiler UI (VK-1529).
//
// It is a process-wide singleton, so every case here pushes a
// full kHistorySize worth of samples rather than assuming it
// starts empty -- that makes each case independent of ordering.
// ============================================================

using render::GpuFrameStats;
using render::GpuPassStats;
using render::GpuPassTiming;

namespace
{
    // Fill the ring with count samples valued 0, 1, 2, ... count-1.
    void pushRamp(GpuPassStats& sink, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            sink.publishFrameTime(static_cast<float>(i), static_cast<float>(i));
        }
    }
}

TEST_SUITE("GpuPassStats")
{

TEST_CASE("publishFrameTime exposes the frame span lock-free")
{
    auto& sink = GpuPassStats::instance();
    sink.publishFrameTime(6.25f, 6.0f);

    CHECK(sink.hasFrameGpuTime());
    CHECK(sink.frameGpuMs() == doctest::Approx(6.25f));
    CHECK(sink.emaFrameGpuMs() == doctest::Approx(6.0f));
}

TEST_CASE("frameMsHistory returns samples oldest-to-newest")
{
    auto& sink = GpuPassStats::instance();
    pushRamp(sink, GpuPassStats::kHistorySize);

    auto history = sink.frameMsHistory();
    REQUIRE(history.size() == GpuPassStats::kHistorySize);
    for (size_t i = 0; i < history.size(); ++i)
    {
        CHECK(history[i] == doctest::Approx(static_cast<float>(i)));
    }
}

TEST_CASE("frameMsHistory wraps and keeps only the newest kHistorySize samples")
{
    auto& sink = GpuPassStats::instance();
    constexpr size_t kOverfill = GpuPassStats::kHistorySize + 80;
    pushRamp(sink, kOverfill);

    auto history = sink.frameMsHistory();
    REQUIRE(history.size() == GpuPassStats::kHistorySize);
    // The oldest 80 have been overwritten; the ring holds 80..199 in order.
    CHECK(history.front() == doctest::Approx(static_cast<float>(kOverfill - GpuPassStats::kHistorySize)));
    CHECK(history.back() == doctest::Approx(static_cast<float>(kOverfill - 1)));
    for (size_t i = 1; i < history.size(); ++i)
    {
        CHECK(history[i] > history[i - 1]);
    }
}

TEST_CASE("publish round-trips per-pass timings")
{
    auto& sink = GpuPassStats::instance();

    GpuFrameStats stats;
    stats.passTimings.push_back({"SceneMeshes", 4.0f, 4.2f, false});
    stats.passTimings.push_back({"VT/SVT Update", 0.5f, 0.4f, true});
    stats.totalMs = 6.0f;
    stats.emaTotalMs = 6.1f;
    stats.barrierCount = 12;
    stats.barrierFlushCount = 3;
    stats.droppedSamples = 2;
    stats.valid = true;
    sink.publish(stats);

    auto snap = sink.snapshot();
    REQUIRE(snap.passTimings.size() == 2);
    CHECK(snap.passTimings[0].name == "SceneMeshes");
    CHECK(snap.passTimings[0].isAux == false);
    CHECK(snap.passTimings[1].name == "VT/SVT Update");
    CHECK(snap.passTimings[1].isAux == true);
    CHECK(snap.totalMs == doctest::Approx(6.0f));
    CHECK(snap.emaTotalMs == doctest::Approx(6.1f));
    CHECK(snap.barrierCount == 12);
    CHECK(snap.barrierFlushCount == 3);
    CHECK(snap.droppedSamples == 2);
    CHECK(snap.valid);
}

TEST_CASE("clear drops pass timings but keeps the frame-time history")
{
    // Tier separation: disabling per-pass capture must not blank the status
    // bar's history plot, which tier 1 keeps feeding.
    auto& sink = GpuPassStats::instance();
    pushRamp(sink, GpuPassStats::kHistorySize);

    GpuFrameStats stats;
    stats.passTimings.push_back({"Upscale", 1.0f, 1.0f, false});
    stats.valid = true;
    sink.publish(stats);
    REQUIRE(sink.snapshot().valid);

    sink.clear();

    CHECK(sink.snapshot().passTimings.empty());
    CHECK_FALSE(sink.snapshot().valid);
    CHECK(sink.frameMsHistory().size() == GpuPassStats::kHistorySize);
    CHECK(sink.hasFrameGpuTime());
}

TEST_CASE("requestEnabled round-trips")
{
    // Deliberately does not assert the off-by-default initial state: the sink is a
    // process-wide singleton, so by the time any case runs another may already have
    // written the flag. The default lives in the member initializer.
    auto& sink = GpuPassStats::instance();

    sink.requestEnabled(true);
    CHECK(sink.isEnabledRequested());
    sink.requestEnabled(false);
    CHECK_FALSE(sink.isEnabledRequested());
}

}
