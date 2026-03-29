#include <doctest.h>
#include <types/RenderSettings.hpp>

// ============================================================
// VK-1095: Rendering Config unit tests
// ============================================================

TEST_SUITE("RenderingConfig") {

// ---- ShadowSettings ----

TEST_CASE("ShadowSettings: default quality High means directionalResolution == 2048") {
    types::ShadowSettings shadows;
    CHECK(shadows.quality == types::ShadowQuality::High);
    CHECK(shadows.directionalResolution == 2048);
}

TEST_CASE("ShadowSettings: default bias values are positive") {
    types::ShadowSettings shadows;
    CHECK(shadows.shadowBias > 0.0f);
    CHECK(shadows.slopeBias > 0.0f);
    CHECK(shadows.normalBias > 0.0f);
}

TEST_CASE("ShadowSettings: soft shadows enabled by default") {
    types::ShadowSettings shadows;
    CHECK(shadows.softShadows);
}

TEST_CASE("ShadowSettings: shadow intensity in valid range") {
    types::ShadowSettings shadows;
    CHECK(shadows.shadowIntensity >= 0.0f);
    CHECK(shadows.shadowIntensity <= 1.0f);
}

// ---- DistanceCullingSettings ----

TEST_CASE("DistanceCullingSettings: shadowDistanceMultiplier applied correctly") {
    types::DistanceCullingSettings dc;
    float effectiveShadowDist = dc.staticMeshDistance * dc.shadowDistanceMultiplier;
    // With default multiplier 0.5, shadow distance is half of static mesh distance
    CHECK(effectiveShadowDist == doctest::Approx(dc.staticMeshDistance * 0.5f));
    CHECK(effectiveShadowDist < dc.staticMeshDistance);
}

TEST_CASE("DistanceCullingSettings: billboard distance equals static mesh distance by default") {
    types::DistanceCullingSettings dc;
    // Both default to 1000.0f
    CHECK(dc.billboardDistance == doctest::Approx(dc.staticMeshDistance));
    CHECK(dc.billboardDistance == doctest::Approx(1000.0f));
    CHECK(dc.staticMeshDistance == doctest::Approx(1000.0f));
    // But the effective shadow distance for static mesh is reduced by multiplier
    float shadowDist = dc.staticMeshDistance * dc.shadowDistanceMultiplier;
    CHECK(shadowDist < dc.staticMeshDistance);
}

TEST_CASE("DistanceCullingSettings: all distances are positive") {
    types::DistanceCullingSettings dc;
    CHECK(dc.staticMeshDistance > 0.0f);
    CHECK(dc.terrainDistance > 0.0f);
    CHECK(dc.foliageDistance > 0.0f);
    CHECK(dc.vfxDistance > 0.0f);
    CHECK(dc.decalDistance > 0.0f);
    CHECK(dc.billboardDistance > 0.0f);
    CHECK(dc.waterDistance > 0.0f);
}

TEST_CASE("DistanceCullingSettings: shadowDistanceMultiplier in (0,1]") {
    types::DistanceCullingSettings dc;
    CHECK(dc.shadowDistanceMultiplier > 0.0f);
    CHECK(dc.shadowDistanceMultiplier <= 1.0f);
}

// ---- RenderSettings::createDefault ----

TEST_CASE("RenderSettings: createDefault produces valid sub-settings") {
    auto rs = types::RenderSettings::createDefault();

    // Shadows
    CHECK(rs.shadows.enabled);
    CHECK(rs.shadows.directionalResolution > 0);

    // Culling
    CHECK(rs.culling.frustumCullingEnabled);
    CHECK(rs.culling.occlusionCullingEnabled);

    // Terrain
    CHECK(rs.terrain.enabled);
    CHECK(rs.terrain.lodBias > 0.0f);

    // Distance culling distances positive
    CHECK(rs.distanceCulling.staticMeshDistance > 0.0f);

    // Light streaming
    CHECK(rs.lightStreaming.maxPointLights > 0);
    CHECK(rs.lightStreaming.maxSpotLights > 0);
}

// ---- VFXLODSettings ----

TEST_CASE("VFXLODSettings: distances are monotonically increasing") {
    types::VFXLODSettings vfx;
    CHECK(vfx.lod0Distance < vfx.lod1Distance);
    CHECK(vfx.lod1Distance < vfx.lod2Distance);
}

TEST_CASE("VFXLODSettings: all distances are positive") {
    types::VFXLODSettings vfx;
    CHECK(vfx.lod0Distance > 0.0f);
    CHECK(vfx.lod1Distance > 0.0f);
    CHECK(vfx.lod2Distance > 0.0f);
    CHECK(vfx.transitionZone > 0.0f);
}

// ---- AnimationLODSettings ----

TEST_CASE("AnimationLODSettings: distances are monotonically increasing") {
    types::AnimationLODSettings anim;
    CHECK(anim.lod0Distance < anim.lod1Distance);
    CHECK(anim.lod1Distance < anim.lod2Distance);
    CHECK(anim.lod2Distance < anim.lod3Distance);
}

TEST_CASE("AnimationLODSettings: all distances are positive") {
    types::AnimationLODSettings anim;
    CHECK(anim.lod0Distance > 0.0f);
    CHECK(anim.lod1Distance > 0.0f);
    CHECK(anim.lod2Distance > 0.0f);
    CHECK(anim.lod3Distance > 0.0f);
}

TEST_CASE("AnimationLODSettings: intervals are positive and increasing") {
    types::AnimationLODSettings anim;
    CHECK(anim.lod0Interval >= 1);
    CHECK(anim.lod1Interval >= anim.lod0Interval);
    CHECK(anim.lod2Interval >= anim.lod1Interval);
}

} // TEST_SUITE
