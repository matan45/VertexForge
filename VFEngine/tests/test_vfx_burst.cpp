#include <doctest.h>
#include <vfx/VFXBurstTypes.hpp>
#include <array>
#include <limits>

// ============================================================
// VFX burst emission schedule tests (stateless window evaluation)
// ============================================================

namespace
{
    float never() { return 1.0f; }   // probability roll that always fails (> p for p < 1)
    float always() { return 0.0f; }  // probability roll that always passes
}

TEST_SUITE("VFXBurst") {

TEST_CASE("single burst fires once when window crosses its time") {
    std::vector<vfx::VFXBurst> bursts{{0.0f, 100, 1, 0.5f, 1.0f}};
    CHECK(vfx::evaluateBurstSpawns(bursts, 0.0f, 0.016f, always) == 100);
}

TEST_CASE("single burst does not fire twice across consecutive windows") {
    std::vector<vfx::VFXBurst> bursts{{0.5f, 50, 1, 0.5f, 1.0f}};
    uint32_t total = 0;
    float t = 0.0f;
    const float dt = 0.1f;
    for (int i = 0; i < 20; ++i)
    {
        total += vfx::evaluateBurstSpawns(bursts, t, t + dt, always);
        t += dt;
    }
    CHECK(total == 50);
}

TEST_CASE("burst before window start does not fire") {
    std::vector<vfx::VFXBurst> bursts{{0.0f, 100, 1, 0.5f, 1.0f}};
    CHECK(vfx::evaluateBurstSpawns(bursts, 0.5f, 0.6f, always) == 0);
}

TEST_CASE("empty or degenerate window spawns nothing") {
    std::vector<vfx::VFXBurst> bursts{{0.0f, 100, 1, 0.5f, 1.0f}};
    CHECK(vfx::evaluateBurstSpawns(bursts, 0.5f, 0.5f, always) == 0);
    CHECK(vfx::evaluateBurstSpawns(bursts, 0.5f, 0.4f, always) == 0);
}

TEST_CASE("multi-cycle burst fires every interval") {
    // 3 cycles at t = 1.0, 1.5, 2.0
    std::vector<vfx::VFXBurst> bursts{{1.0f, 10, 3, 0.5f, 1.0f}};

    SUBCASE("all cycles in one large window") {
        CHECK(vfx::evaluateBurstSpawns(bursts, 0.0f, 3.0f, always) == 30);
    }

    SUBCASE("cycles split across small frames sum to total") {
        uint32_t total = 0;
        float t = 0.0f;
        const float dt = 0.05f;
        while (t < 3.0f)
        {
            total += vfx::evaluateBurstSpawns(bursts, t, t + dt, always);
            t += dt;
        }
        CHECK(total == 30);
    }

    SUBCASE("no cycles after the schedule completes") {
        CHECK(vfx::evaluateBurstSpawns(bursts, 2.5f, 10.0f, always) == 0);
    }
}

TEST_CASE("infinite cycles (cycles=0) keep firing") {
    std::vector<vfx::VFXBurst> bursts{{0.0f, 5, 0, 1.0f, 1.0f}};
    // Window [10, 13) -> cycles at t = 10, 11, 12
    CHECK(vfx::evaluateBurstSpawns(bursts, 10.0f, 13.0f, always) == 15);
}

TEST_CASE("probability zero never fires, one always fires") {
    std::vector<vfx::VFXBurst> zeroProb{{0.0f, 100, 1, 0.5f, 0.0f}};
    CHECK(vfx::evaluateBurstSpawns(zeroProb, 0.0f, 1.0f, always) == 0);

    std::vector<vfx::VFXBurst> fullProb{{0.0f, 100, 1, 0.5f, 1.0f}};
    CHECK(vfx::evaluateBurstSpawns(fullProb, 0.0f, 1.0f, never) == 100);
}

TEST_CASE("probability roll gates each cycle") {
    std::vector<vfx::VFXBurst> bursts{{0.0f, 10, 3, 1.0f, 0.5f}};
    CHECK(vfx::evaluateBurstSpawns(bursts, 0.0f, 3.0f, always) == 30);
    CHECK(vfx::evaluateBurstSpawns(bursts, 0.0f, 3.0f, never) == 0);
}

TEST_CASE("non-positive count or interval handled safely") {
    std::vector<vfx::VFXBurst> zeroCount{{0.0f, 0, 1, 0.5f, 1.0f}};
    CHECK(vfx::evaluateBurstSpawns(zeroCount, 0.0f, 1.0f, always) == 0);

    // interval <= 0 with multiple cycles collapses to a single fire
    std::vector<vfx::VFXBurst> zeroInterval{{0.5f, 25, 5, 0.0f, 1.0f}};
    CHECK(vfx::evaluateBurstSpawns(zeroInterval, 0.0f, 1.0f, always) == 25);
}

TEST_CASE("multiple bursts accumulate") {
    std::vector<vfx::VFXBurst> bursts{
        {0.0f, 100, 1, 0.5f, 1.0f},  // explosion pop
        {0.2f, 20, 2, 0.3f, 1.0f},   // sparks at 0.2 and 0.5
    };
    CHECK(vfx::evaluateBurstSpawns(bursts, 0.0f, 1.0f, always) == 140);
}

TEST_CASE("runaway tiny intervals are bounded per window") {
    std::vector<vfx::VFXBurst> bursts{{0.0f, 1, 0, 0.0001f, 1.0f}};
    uint32_t spawned = vfx::evaluateBurstSpawns(bursts, 0.0f, 10.0f, always);
    CHECK(spawned <= static_cast<uint32_t>(vfx::BurstDefaults::MAX_CYCLES_PER_WINDOW));
}

TEST_CASE("burst looping is opt-in: only a positive configured duration sets a period") {
    // review #5 — loopDuration == 0 no longer derives a period from the lifetime/schedule; it
    // means "no burst re-arm" (finite bursts fire once). Only a positive authored value loops.
    const std::vector<vfx::VFXBurst> repro{{0.15f, 700, 4, 1.0f, 1.0f}};
    CHECK(vfx::resolveBurstLoopPeriod(repro, 0.0f, 2.0f) == 0.0f);
    CHECK(vfx::resolveBurstLoopPeriod(repro, 1.25f, 2.0f) == doctest::Approx(1.25f));

    const std::vector<vfx::VFXBurst> immediate{{0.0f, 1, 1, 0.0f, 1.0f}};
    CHECK(vfx::resolveBurstLoopPeriod(immediate, 0.0f, 2.0f) == 0.0f);

    const std::vector<vfx::VFXBurst> empty;
    CHECK(vfx::resolveBurstLoopPeriod(empty, 0.0f, 3.0f) == 0.0f);

    const std::vector<vfx::VFXBurst> disabled{{100.0f, 0, 1, 1.0f, 1.0f}};
    CHECK(vfx::resolveBurstLoopPeriod(disabled, 0.0f, 2.0f) == 0.0f);
}

TEST_CASE("a positive configured loop period is honored verbatim; zero disables wrapping") {
    // review #5 — the derive-from-lifetime path is gone: an explicit positive period is returned
    // as-is (the author's choice), and 0 means no wrapping regardless of the burst schedule.
    const std::vector<vfx::VFXBurst> collapsed{{5.0f, 1, 4, 0.0f, 1.0f}};
    CHECK(vfx::resolveBurstLoopPeriod(collapsed, 3.0f, 2.0f) == doctest::Approx(3.0f));
    CHECK(vfx::resolveBurstLoopPeriod(collapsed, 0.0f, 2.0f) == 0.0f);

    const std::vector<vfx::VFXBurst> onlyAtZero{{0.0f, 1, 1, 0.0f, 1.0f}};
    CHECK(vfx::resolveBurstLoopPeriod(onlyAtZero, 0.0f, 0.0f) == 0.0f);
}

TEST_CASE("invalid configured loop periods disable wrapping") {
    const std::vector<vfx::VFXBurst> bursts{{0.0f, 1, 1, 1.0f, 1.0f}};
    CHECK(vfx::resolveBurstLoopPeriod(bursts, -1.0f, 2.0f) == 0.0f);
    CHECK(vfx::resolveBurstLoopPeriod(
        bursts, std::numeric_limits<float>::infinity(), 2.0f) == 0.0f);
    CHECK(vfx::resolveBurstLoopPeriod(
        bursts, std::numeric_limits<float>::quiet_NaN(), 2.0f) == 0.0f);
}

TEST_CASE("finite bursts re-arm once per loop period") {
    const std::vector<vfx::VFXBurst> repro{{0.15f, 700, 4, 1.0f, 1.0f}};
    constexpr float period = 4.15f;

    CHECK(vfx::evaluateBurstSpawnsLooped(repro, 0.0f, period, period, always) == 2800);
    CHECK(vfx::evaluateBurstSpawnsLooped(repro, period, 8.30f, period, always) == 2800);
    CHECK(vfx::evaluateBurstSpawnsLooped(repro, 0.0f, 12.45f, period, always) == 8400);

    CHECK(vfx::evaluateBurstSpawnsLooped(repro, 4.145f, 4.155f, period, always) == 0);
    CHECK(vfx::evaluateBurstSpawnsLooped(repro, 4.29f, 4.31f, period, always) == 700);
}

TEST_CASE("loop seams preserve half-open window behavior") {
    const std::vector<vfx::VFXBurst> bursts{{0.0f, 1, 1, 0.5f, 1.0f}};
    constexpr float period = 1.0f;
    const float before = std::nextafter(period, 0.0f);
    const float after = std::nextafter(period, 2.0f);

    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, 0.0f, period, period, always) == 1);
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, period, 2.0f, period, always) == 1);
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, before, period, period, always) == 0);
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, period, after, period, always) == 1);
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, before, after, period, always) == 1);
}

TEST_CASE("unbounded bursts remain on their absolute schedule while finite bursts loop") {
    const std::vector<vfx::VFXBurst> bursts{
        {0.15f, 700, 4, 1.0f, 1.0f},
        {0.0f, 5, 0, 1.0f, 1.0f},
    };
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, 0.0f, 8.30f, 4.15f, always) == 5645);

    const std::vector<vfx::VFXBurst> negativeCycles{{0.0f, 5, -1, 1.0f, 1.0f}};
    CHECK(vfx::evaluateBurstSpawnsLooped(
        negativeCycles, 0.0f, 8.30f, 4.15f, always) == 45);
}

TEST_CASE("looped probability rolls stay burst-major and chronological") {
    const std::vector<vfx::VFXBurst> bursts{
        {0.0f, 1, 1, 0.5f, 0.5f},
        {0.0f, 10, 1, 0.5f, 0.5f},
    };
    const std::array<float, 4> rolls{0.0f, 0.0f, 1.0f, 1.0f};
    size_t rollIndex = 0;
    auto sequence = [&]() { return rolls.at(rollIndex++); };

    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, 0.0f, 2.0f, 1.0f, sequence) == 2);
    CHECK(rollIndex == rolls.size());
}

TEST_CASE("looped evaluation shares one safety budget across all period slices") {
    const std::vector<vfx::VFXBurst> bursts{{0.0f, 1, 64, 0.001f, 0.5f}};
    int rolls = 0;
    auto countingAlways = [&]() {
        ++rolls;
        return 0.0f;
    };

    const uint32_t spawned = vfx::evaluateBurstSpawnsLooped(
        bursts, 0.0f, 10.0f, 0.064f, countingAlways);
    CHECK(spawned == static_cast<uint32_t>(vfx::BurstDefaults::MAX_CYCLES_PER_WINDOW));
    CHECK(rolls == vfx::BurstDefaults::MAX_CYCLES_PER_WINDOW);
}

TEST_CASE("looped evaluation handles negative time and invalid period fallback") {
    const std::vector<vfx::VFXBurst> bursts{{0.0f, 3, 1, 0.5f, 1.0f}};
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, -1.0f, 0.0f, 1.0f, always) == 0);
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, -0.1f, 0.1f, 1.0f, always) == 3);

    const uint32_t ordinary = vfx::evaluateBurstSpawns(bursts, 0.0f, 2.0f, always);
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, 0.0f, 2.0f, 0.0f, always) == ordinary);
    CHECK(vfx::evaluateBurstSpawnsLooped(
        bursts, 0.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), always) == ordinary);
    CHECK(vfx::evaluateBurstSpawnsLooped(
        bursts, 0.0f, 2.0f, std::numeric_limits<float>::infinity(), always) == ordinary);

    CHECK(vfx::evaluateBurstSpawnsLooped(
        bursts, 1'000'000.0f, 1'000'001.0f, 1.0f, always) == 3);
}

TEST_CASE("degenerate windows and guaranteed probabilities consume no RNG") {
    const std::vector<vfx::VFXBurst> bursts{{0.0f, 1, 1, 0.5f, 1.0f}};
    int rolls = 0;
    auto counting = [&]() {
        ++rolls;
        return 0.0f;
    };
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, 1.0f, 1.0f, 1.0f, counting) == 0);
    CHECK(vfx::evaluateBurstSpawnsLooped(bursts, 0.0f, 2.0f, 1.0f, counting) == 2);
    CHECK(rolls == 0);
}

// ---- node property round-trip ----

TEST_CASE("store/load bursts round-trips through node properties") {
    vfx::VFXNode node;
    node.type = vfx::VFXNodeType::Emitter;

    std::vector<vfx::VFXBurst> bursts{
        {0.0f, 200, 1, 0.5f, 1.0f},
        {1.5f, 30, 4, 0.25f, 0.75f},
    };
    vfx::storeBurstsToNode(node, bursts);

    auto loaded = vfx::loadBurstsFromNode(node);
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].time == doctest::Approx(0.0f));
    CHECK(loaded[0].count == 200);
    CHECK(loaded[0].cycles == 1);
    CHECK(loaded[1].time == doctest::Approx(1.5f));
    CHECK(loaded[1].count == 30);
    CHECK(loaded[1].cycles == 4);
    CHECK(loaded[1].interval == doctest::Approx(0.25f));
    CHECK(loaded[1].probability == doctest::Approx(0.75f));
}

TEST_CASE("storing a smaller list clears stale trailing properties") {
    vfx::VFXNode node;
    node.type = vfx::VFXNodeType::Emitter;

    std::vector<vfx::VFXBurst> three(3, vfx::VFXBurst{});
    vfx::storeBurstsToNode(node, three);
    CHECK(node.properties.count("burst2Time") == 1);

    std::vector<vfx::VFXBurst> one(1, vfx::VFXBurst{});
    vfx::storeBurstsToNode(node, one);
    CHECK(node.properties.count("burst2Time") == 0);
    CHECK(vfx::loadBurstsFromNode(node).size() == 1);
}

TEST_CASE("node without burst properties loads empty list") {
    vfx::VFXNode node;
    node.type = vfx::VFXNodeType::Emitter;
    CHECK(vfx::loadBurstsFromNode(node).empty());
}

}
