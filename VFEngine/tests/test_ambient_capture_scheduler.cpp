// VK-1569 — Time-slicing scheduler for dynamic sky -> IBL ambient capture.
// Pure CPU state machine: full-cycle completeness, budget invariance, publish-only-after-complete,
// restart-on-invalidate, and index/phase boundary mapping.
#include "doctest.h"

#include <atmosphere/AmbientCaptureScheduler.hpp>

#include <vector>
#include <cstdint>

using render::atmosphere::AmbientCapturePlan;
using render::atmosphere::AmbientCaptureScheduler;
using render::atmosphere::CapturePhase;
using render::atmosphere::WorkItem;

namespace
{
    // Drain a fresh scheduler completely at a fixed budget, collecting every emitted item in order.
    std::vector<WorkItem> drainAll(uint32_t budget)
    {
        AmbientCaptureScheduler s;
        std::vector<WorkItem> items;
        while (!s.cycleComplete())
        {
            const uint32_t n = s.advance(budget, [&](const WorkItem& w) { items.push_back(w); });
            CHECK(n >= 1u); // must make progress until complete
            CHECK(n <= budget);
        }
        return items;
    }
}

TEST_CASE("ambient capture: plan item counts")
{
    AmbientCapturePlan plan;
    CHECK(plan.envFaces == 6u);
    CHECK(plan.irrFaces == 6u);
    CHECK(plan.prefilterFaces == 6u);
    CHECK(plan.prefilterMips == 5u);
    CHECK(plan.prefilterItems() == 30u);
    CHECK(plan.totalItems() == 42u);
}

TEST_CASE("ambient capture: a full cycle at budget 1 covers every item exactly once")
{
    const std::vector<WorkItem> items = drainAll(1);
    CHECK(items.size() == 42u);

    int envFace[6] = {0, 0, 0, 0, 0, 0};
    int irrFace[6] = {0, 0, 0, 0, 0, 0};
    int prefilter[6][5] = {};

    for (const WorkItem& w : items)
    {
        switch (w.phase)
        {
        case CapturePhase::Env:
            REQUIRE(w.face < 6u);
            ++envFace[w.face];
            break;
        case CapturePhase::Irradiance:
            REQUIRE(w.face < 6u);
            ++irrFace[w.face];
            break;
        case CapturePhase::Prefilter:
            REQUIRE(w.face < 6u);
            REQUIRE(w.mip < 5u);
            ++prefilter[w.face][w.mip];
            break;
        }
    }

    for (int f = 0; f < 6; ++f)
    {
        CHECK(envFace[f] == 1);
        CHECK(irrFace[f] == 1);
        for (int m = 0; m < 5; ++m)
            CHECK(prefilter[f][m] == 1);
    }
}

TEST_CASE("ambient capture: budget is invariant — same ordered sequence regardless of budget")
{
    const std::vector<WorkItem> b1 = drainAll(1);
    const std::vector<WorkItem> b7 = drainAll(7);   // 42 / 7 = 6 exact advances
    const std::vector<WorkItem> b100 = drainAll(100); // one advance, clamped to 42

    REQUIRE(b1.size() == b7.size());
    REQUIRE(b1.size() == b100.size());
    for (size_t i = 0; i < b1.size(); ++i)
    {
        CHECK(b1[i].phase == b7[i].phase);
        CHECK(b1[i].face == b7[i].face);
        CHECK(b1[i].mip == b7[i].mip);
        CHECK(b1[i].phase == b100[i].phase);
        CHECK(b1[i].face == b100[i].face);
        CHECK(b1[i].mip == b100[i].mip);
    }
}

TEST_CASE("ambient capture: publish only after a complete cycle")
{
    AmbientCaptureScheduler s;
    CHECK_FALSE(s.shouldPublish()); // nothing captured yet

    // Partial progress must not enable publishing.
    s.advance(20, [](const WorkItem&) {});
    CHECK(s.cursor == 20u);
    CHECK_FALSE(s.cycleComplete());
    CHECK_FALSE(s.shouldPublish());

    // Finish the cycle.
    s.advance(1000, [](const WorkItem&) {});
    CHECK(s.cursor == 42u);
    CHECK(s.cycleComplete());
    CHECK(s.shouldPublish());

    s.markPublished();
    CHECK_FALSE(s.shouldPublish()); // published exactly once
    CHECK(s.cycleComplete());       // still complete
}

TEST_CASE("ambient capture: restart on invalidation discards the partial cycle and never publishes it")
{
    AmbientCaptureScheduler s;
    s.restart(100); // start capturing sky-state epoch 100
    s.advance(30, [](const WorkItem&) {});
    CHECK(s.cursor == 30u);

    // The sky changed (new epoch) mid-cycle.
    CHECK(s.needsRestart(200));
    CHECK_FALSE(s.needsRestart(100));

    s.restart(200);
    CHECK(s.cursor == 0u);
    CHECK(s.published == false);
    CHECK(s.remaining() == 42u);
    CHECK_FALSE(s.shouldPublish());      // a fresh cycle cannot publish
    CHECK_FALSE(s.needsRestart(200));    // now aligned to the new epoch
    CHECK(s.epoch == 200u);
}

TEST_CASE("ambient capture: linear index maps to the right phase/face/mip at the boundaries")
{
    AmbientCaptureScheduler s;

    auto item = [&](uint32_t i) { return s.itemAt(i); };

    CHECK(item(0).phase == CapturePhase::Env);
    CHECK(item(0).face == 0u);
    CHECK(item(5).phase == CapturePhase::Env);
    CHECK(item(5).face == 5u);

    CHECK(item(6).phase == CapturePhase::Irradiance);
    CHECK(item(6).face == 0u);
    CHECK(item(11).phase == CapturePhase::Irradiance);
    CHECK(item(11).face == 5u);

    // Prefilter is mip-major: mip 0 faces 0..5, then mip 1 faces 0..5, ...
    CHECK(item(12).phase == CapturePhase::Prefilter);
    CHECK(item(12).face == 0u);
    CHECK(item(12).mip == 0u);
    CHECK(item(17).face == 5u);
    CHECK(item(17).mip == 0u);
    CHECK(item(18).face == 0u);
    CHECK(item(18).mip == 1u);
    CHECK(item(41).face == 5u);
    CHECK(item(41).mip == 4u);
}

TEST_CASE("ambient capture: a budget larger than the remainder emits only the remainder")
{
    AmbientCaptureScheduler s;
    s.advance(40, [](const WorkItem&) {});
    CHECK(s.remaining() == 2u);

    uint32_t emitted = 0;
    const uint32_t n = s.advance(100, [&](const WorkItem&) { ++emitted; });
    CHECK(n == 2u);
    CHECK(emitted == 2u);
    CHECK(s.cursor == 42u);
    CHECK(s.cycleComplete());

    // Advancing a completed cycle emits nothing.
    const uint32_t none = s.advance(10, [](const WorkItem&) {});
    CHECK(none == 0u);
    CHECK(s.cursor == 42u);
}
