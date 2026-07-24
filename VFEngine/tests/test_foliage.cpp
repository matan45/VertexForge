#include <doctest.h>
#include <foliage/FoliageTypes.hpp>
#include <foliage/FoliageCompose.hpp>
#include <terrain/TerrainTile.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
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
    CHECK(type.affectedByDensityScale); // VK-1582: default opted-in to the global density scale
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

// ============================================================
// VK-1573: collector pure-math helpers (FoliageCompose.hpp)
// ============================================================

TEST_CASE("unpackTintRGBA8 decodes 0xRRGGBBAA into a normalized vec4") {
    // Opaque white (default tint) round-trips to all-ones.
    glm::vec4 white = foliage::unpackTintRGBA8(0xFFFFFFFFu);
    CHECK(white.r == doctest::Approx(1.0f));
    CHECK(white.g == doctest::Approx(1.0f));
    CHECK(white.b == doctest::Approx(1.0f));
    CHECK(white.a == doctest::Approx(1.0f));

    // R in the most-significant byte.
    glm::vec4 red = foliage::unpackTintRGBA8(0xFF000000u);
    CHECK(red.r == doctest::Approx(1.0f));
    CHECK(red.g == doctest::Approx(0.0f));
    CHECK(red.b == doctest::Approx(0.0f));
    CHECK(red.a == doctest::Approx(0.0f));

    // Green with full alpha locks the middle two bytes + LSB.
    glm::vec4 green = foliage::unpackTintRGBA8(0x00FF00FFu);
    CHECK(green.r == doctest::Approx(0.0f));
    CHECK(green.g == doctest::Approx(1.0f));
    CHECK(green.b == doctest::Approx(0.0f));
    CHECK(green.a == doctest::Approx(1.0f));

    // Half grey (0x80) is ~0.5019.
    glm::vec4 grey = foliage::unpackTintRGBA8(0x80808080u);
    CHECK(grey.r == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("composeFoliageModelMatrix builds Translate*RotateY*Scale") {
    foliage::FoliageType type; // alignToNormal defaults false

    SUBCASE("identity instance yields pure translation") {
        foliage::FoliageInstance fi;
        fi.position = glm::vec3(3.0f, 5.0f, -7.0f);
        glm::mat4 m = foliage::composeFoliageModelMatrix(fi, type);
        CHECK(m[3].x == doctest::Approx(3.0f));
        CHECK(m[3].y == doctest::Approx(5.0f));
        CHECK(m[3].z == doctest::Approx(-7.0f));
        // No rotation/scale: basis is identity.
        CHECK(m[0].x == doctest::Approx(1.0f));
        CHECK(m[1].y == doctest::Approx(1.0f));
        CHECK(m[2].z == doctest::Approx(1.0f));
    }

    SUBCASE("non-uniform scale is applied to the basis") {
        foliage::FoliageInstance fi;
        fi.scale = glm::vec3(2.0f, 3.0f, 4.0f);
        glm::mat4 m = foliage::composeFoliageModelMatrix(fi, type);
        CHECK(glm::length(glm::vec3(m[0])) == doctest::Approx(2.0f));
        CHECK(glm::length(glm::vec3(m[1])) == doctest::Approx(3.0f));
        CHECK(glm::length(glm::vec3(m[2])) == doctest::Approx(4.0f));
    }

    SUBCASE("rotationY = pi flips the X and Z basis vectors") {
        foliage::FoliageInstance fi;
        fi.rotationY = glm::pi<float>();
        glm::mat4 m = foliage::composeFoliageModelMatrix(fi, type);
        // Rotating +X by 180 deg about Y gives -X; +Z gives -Z.
        CHECK(m[0].x == doctest::Approx(-1.0f).epsilon(0.0001));
        CHECK(m[2].z == doctest::Approx(-1.0f).epsilon(0.0001));
    }

    SUBCASE("alignToNormal tilts the local up-axis onto the surface normal") {
        foliage::FoliageType tiltType;
        tiltType.alignToNormal = true;
        foliage::FoliageInstance fi;
        fi.normal = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f)); // 45 deg slope
        glm::mat4 m = foliage::composeFoliageModelMatrix(fi, tiltType);
        glm::vec3 localUp = glm::normalize(glm::vec3(m[1]));
        CHECK(localUp.x == doctest::Approx(fi.normal.x).epsilon(0.001));
        CHECK(localUp.y == doctest::Approx(fi.normal.y).epsilon(0.001));
        CHECK(localUp.z == doctest::Approx(fi.normal.z).epsilon(0.001));
    }
}

TEST_CASE("foliageTileInRange culls on the XZ plane, ignoring Y") {
    glm::vec3 tileCenter(100.0f, 0.0f, 0.0f);
    glm::vec3 camNear(90.0f, 999.0f, 0.0f);  // 10 units away in XZ, huge Y offset
    glm::vec3 camFar(40.0f, 0.0f, 0.0f);     // 60 units away in XZ
    CHECK(foliage::foliageTileInRange(tileCenter, camNear, 50.0f));
    CHECK_FALSE(foliage::foliageTileInRange(tileCenter, camFar, 50.0f));
    // Exactly on the boundary is inclusive.
    CHECK(foliage::foliageTileInRange(tileCenter, glm::vec3(50.0f, 0.0f, 0.0f), 50.0f));
}

TEST_CASE("groupInstanceIndicesByType buckets by type and drops out-of-range indices") {
    std::vector<foliage::FoliageInstance> instances(5);
    instances[0].typeIndex = 0;
    instances[1].typeIndex = 2;
    instances[2].typeIndex = 0;
    instances[3].typeIndex = 2;
    instances[4].typeIndex = 7; // out of range for a palette of size 3

    auto groups = foliage::groupInstanceIndicesByType(instances, /*paletteSize*/ 3);
    CHECK(groups.size() == 2u);             // only types 0 and 2 survive
    CHECK(groups[0].size() == 2u);
    CHECK(groups[2].size() == 2u);
    CHECK(groups.find(7) == groups.end());  // out-of-range dropped
}

} // TEST_SUITE("Foliage")
