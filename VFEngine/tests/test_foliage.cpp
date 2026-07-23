#include <doctest.h>
#include <foliage/FoliageTypes.hpp>
#include <terrain/TerrainTile.hpp>
#include <glm/glm.hpp>
#include <type_traits>
#include <cstdint>

// ============================================================
// VK-1571: Foliage packed per-tile instance store + FoliageType palette
// ============================================================

TEST_SUITE("Foliage") {

// ---- FoliageInstance ABI freeze ----
// The header already static_asserts these; re-checking at runtime documents the
// contract and keeps the guarantee visible in the test report.

TEST_CASE("FoliageInstance is a frozen 56-byte, 4-aligned, trivially-copyable POD") {
    CHECK(sizeof(foliage::FoliageInstance) == 56);
    CHECK(alignof(foliage::FoliageInstance) == 4);
    CHECK(std::is_trivially_copyable_v<foliage::FoliageInstance>);
    CHECK(std::is_standard_layout_v<foliage::FoliageInstance>);
}

TEST_CASE("FoliageInstance default values are sane") {
    foliage::FoliageInstance inst;
    CHECK(inst.position.x == doctest::Approx(0.0f));
    CHECK(inst.position.y == doctest::Approx(0.0f));
    CHECK(inst.position.z == doctest::Approx(0.0f));
    CHECK(inst.rotationY == doctest::Approx(0.0f));
    CHECK(inst.scale.x == doctest::Approx(1.0f));
    CHECK(inst.scale.y == doctest::Approx(1.0f));
    CHECK(inst.scale.z == doctest::Approx(1.0f));
    CHECK(inst.typeIndex == 0u);
    CHECK(inst.flags == foliage::FoliageInstanceFlags::None);
    CHECK(inst.normal.x == doctest::Approx(0.0f));
    CHECK(inst.normal.y == doctest::Approx(1.0f));
    CHECK(inst.normal.z == doctest::Approx(0.0f));
    CHECK(inst.windPhase == doctest::Approx(0.0f));
    CHECK(inst.tint == 0xFFFFFFFFu); // opaque white = no tint
    CHECK(inst.seed == 0u);
}

// ---- Flags ----

TEST_CASE("FoliageInstanceFlags compose and test as a bitmask") {
    const uint16_t flags = static_cast<uint16_t>(
        foliage::FoliageInstanceFlags::Tilt | foliage::FoliageInstanceFlags::NavContribute);
    CHECK((flags & foliage::FoliageInstanceFlags::Tilt) != 0);
    CHECK((flags & foliage::FoliageInstanceFlags::NavContribute) != 0);
    CHECK((flags & foliage::FoliageInstanceFlags::Collider) == 0);
}

// ---- FoliageType palette entry ----

TEST_CASE("FoliageType default values match the palette contract") {
    foliage::FoliageType type;
    CHECK(type.meshPath.empty());
    CHECK(type.materialPath.empty());
    CHECK(type.weight == doctest::Approx(1.0f));
    CHECK(type.densityScale == doctest::Approx(1.0f));
    CHECK(type.scaleRange.x == doctest::Approx(0.8f));
    CHECK(type.scaleRange.y == doctest::Approx(1.2f));
    CHECK(type.heightRange.x == doctest::Approx(1.0f));
    CHECK(type.heightRange.y == doctest::Approx(1.0f));
    CHECK(type.rotationYRange.x == doctest::Approx(0.0f));
    CHECK(type.rotationYRange.y == doctest::Approx(360.0f));
    CHECK(type.randomTilt == doctest::Approx(0.0f));
    CHECK_FALSE(type.alignToNormal);
    CHECK(type.minSlopeDeg == doctest::Approx(0.0f));
    CHECK(type.maxSlopeDeg == doctest::Approx(90.0f));
    CHECK(type.startCullDistance == doctest::Approx(0.0f));
    CHECK(type.endCullDistance == doctest::Approx(200.0f));
    CHECK(type.castShadow);
    CHECK(type.farMode == foliage::FoliageFarMode::LOD3Cutoff);
    CHECK_FALSE(type.receiveWind);
    CHECK(type.windStrength == doctest::Approx(1.0f));
    CHECK(type.windStiffness == doctest::Approx(1.0f));
    CHECK_FALSE(type.collision);
    CHECK_FALSE(type.navContribute);
    CHECK(type.visible);
    CHECK(type.paintEnabled);
}

TEST_CASE("Foliage palette cap is 64 and typeIndex covers the whole range") {
    CHECK(foliage::MAX_FOLIAGE_TYPES == 64);
    foliage::FoliageInstance inst;
    inst.typeIndex = static_cast<uint16_t>(foliage::MAX_FOLIAGE_TYPES - 1); // 63 = highest valid slot
    CHECK(inst.typeIndex == 63u);
}

// ---- TerrainTile storage (mirrors the billboard triplet) ----

TEST_CASE("TerrainTile stores foliage instances entity-free") {
    terrain::TerrainTile tile;
    CHECK_FALSE(tile.hasFoliageInstances());
    CHECK_FALSE(tile.foliageInstancesDirty);
    CHECK_FALSE(tile.foliageInstancesGPUDirty);

    foliage::FoliageInstance inst;
    inst.position = glm::vec3(1.0f, 2.0f, 3.0f);
    inst.typeIndex = 5;
    tile.foliageInstances.push_back(inst);

    CHECK(tile.hasFoliageInstances());
    CHECK(tile.foliageInstances.size() == 1u);
    CHECK(tile.foliageInstances[0].typeIndex == 5u);
    CHECK(tile.foliageInstances[0].position.y == doctest::Approx(2.0f));
}

} // TEST_SUITE("Foliage")
