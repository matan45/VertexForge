#include <doctest.h>
#include <physics/SpatialQueryHelpers.hpp>
#include <cstdint>
#include <vector>

// ============================================================
// Phase 1 spatial overlap queries — pure-logic unit tests.
//
// The Tests project is CPU-only and does not link Jolt, so the actual
// CollideShape narrow-phase path cannot be exercised here (it needs a live
// PhysicsSystem + JPH factory/allocators that this harness does not set up).
// These tests cover the two pieces of logic that are independent of Jolt and
// were deliberately factored into physics/SpatialQueryHelpers.hpp so they are
// testable:
//   * layerMaskAllows  — the LayerMaskFilter collision predicate
//   * dedupeEntityIds  — collapsing multi-sub-shape hits to one id per entity
// ============================================================

using core::physics::layerMaskAllows;
using core::physics::dedupeEntityIds;

TEST_SUITE("PhysicsOverlap") {

// ---- layerMaskAllows truth table ----

TEST_CASE("layerMaskAllows: 0xFFFF passes every valid layer") {
    for (uint32_t layer = 0; layer < 16; ++layer) {
        CHECK(layerMaskAllows(0xFFFF, layer));
    }
}

TEST_CASE("layerMaskAllows: single-bit mask passes only its layer") {
    // Bit 3 set -> only layer 3 collides.
    const uint16_t mask = static_cast<uint16_t>(1u << 3);
    for (uint32_t layer = 0; layer < 16; ++layer) {
        if (layer == 3) {
            CHECK(layerMaskAllows(mask, layer));
        } else {
            CHECK_FALSE(layerMaskAllows(mask, layer));
        }
    }
}

TEST_CASE("layerMaskAllows: multi-bit mask passes exactly its set layers") {
    // Layers 0, 5 and 15 enabled.
    const uint16_t mask = static_cast<uint16_t>((1u << 0) | (1u << 5) | (1u << 15));
    CHECK(layerMaskAllows(mask, 0));
    CHECK(layerMaskAllows(mask, 5));
    CHECK(layerMaskAllows(mask, 15));
    CHECK_FALSE(layerMaskAllows(mask, 1));
    CHECK_FALSE(layerMaskAllows(mask, 6));
    CHECK_FALSE(layerMaskAllows(mask, 14));
}

TEST_CASE("layerMaskAllows: zero mask rejects everything") {
    for (uint32_t layer = 0; layer < 16; ++layer) {
        CHECK_FALSE(layerMaskAllows(0, layer));
    }
}

TEST_CASE("layerMaskAllows: layer index >= 16 is always rejected") {
    // Even with an all-ones mask, out-of-range layers must not collide
    // (only 16 collision layers fit in the uint16_t mask).
    CHECK_FALSE(layerMaskAllows(0xFFFF, 16));
    CHECK_FALSE(layerMaskAllows(0xFFFF, 17));
    CHECK_FALSE(layerMaskAllows(0xFFFF, 31));
    CHECK_FALSE(layerMaskAllows(0xFFFF, 1000));
}

// ---- dedupeEntityIds ----

TEST_CASE("dedupeEntityIds: empty stays empty") {
    std::vector<uint64_t> ids;
    dedupeEntityIds(ids);
    CHECK(ids.empty());
}

TEST_CASE("dedupeEntityIds: already-unique list is unchanged") {
    std::vector<uint64_t> ids{1, 2, 3, 4};
    dedupeEntityIds(ids);
    REQUIRE(ids.size() == 4);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);
    CHECK(ids[2] == 3);
    CHECK(ids[3] == 4);
}

TEST_CASE("dedupeEntityIds: collapses repeats, preserves first-seen order") {
    // A compound/mesh body reports multiple sub-shape hits for the same entity.
    std::vector<uint64_t> ids{7, 7, 3, 7, 3, 9, 9};
    dedupeEntityIds(ids);
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == 7);
    CHECK(ids[1] == 3);
    CHECK(ids[2] == 9);
}

TEST_CASE("dedupeEntityIds: all-identical collapses to one") {
    std::vector<uint64_t> ids{42, 42, 42, 42};
    dedupeEntityIds(ids);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 42);
}

TEST_CASE("dedupeEntityIds: entity id 0 is preserved (entt entity 0 is valid)") {
    std::vector<uint64_t> ids{0, 0, 5};
    dedupeEntityIds(ids);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 0);
    CHECK(ids[1] == 5);
}

}
