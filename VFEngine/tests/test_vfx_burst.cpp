#include <doctest.h>
#include <vfx/VFXBurstTypes.hpp>

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
