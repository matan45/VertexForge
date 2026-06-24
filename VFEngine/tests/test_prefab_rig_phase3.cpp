#include <doctest.h>

// VK-1433 Phase 3 — debug shading + analytic-lighting plumbing (CPU-testable seams).
//
// The shader output and the wireframe pipeline are GPU-only (need a Vulkan device), so those are
// verified manually in-editor. What IS pure and testable is the mapping layer the controller uses
// to translate the editor's single "Shading:" selection + spherical light angles into the
// SkinnedMeshRenderData fields that get pushed:
//   - resolveSkinnedShading(selection)        -> {SkinnedDebugMode, bool wireframe}
//   - keyLightDirectionFromSpherical(az, el)   -> unit world-space direction
// plus the byte-parity static_asserts on the push-constant struct (compile-time, but exercised
// here via sizeof/offset checks so a regression is loud at runtime too).

#include "render/mesh/SkinnedMeshTypes.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <cstddef>

using namespace render::mesh;

namespace
{
    bool near1(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }
    bool vecNear(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f)
    {
        return near1(a.x, b.x, eps) && near1(a.y, b.y, eps) && near1(a.z, b.z, eps);
    }
}

TEST_CASE("resolveSkinnedShading: each selection maps to the right debug mode + wireframe flag")
{
    // 0 = Lit: the DEFAULT — no debug branch, fill pipeline (this is what preserves today's look).
    {
        const ResolvedSkinnedShading r = resolveSkinnedShading(0);
        CHECK(r.debugMode == SkinnedDebugMode::None);
        CHECK(r.wireframe == false);
    }
    {
        const ResolvedSkinnedShading r = resolveSkinnedShading(1);
        CHECK(r.debugMode == SkinnedDebugMode::Clay);
        CHECK(r.wireframe == false);
    }
    {
        const ResolvedSkinnedShading r = resolveSkinnedShading(2);
        CHECK(r.debugMode == SkinnedDebugMode::Normals);
        CHECK(r.wireframe == false);
    }
    {
        const ResolvedSkinnedShading r = resolveSkinnedShading(3);
        CHECK(r.debugMode == SkinnedDebugMode::UVs);
        CHECK(r.wireframe == false);
    }
    {
        const ResolvedSkinnedShading r = resolveSkinnedShading(4);
        CHECK(r.debugMode == SkinnedDebugMode::AlbedoUnlit);
        CHECK(r.wireframe == false);
    }
    // 5 = Wireframe: NOT a shader branch — the shader stays at debugMode None; the wireframe is the
    // PolygonMode::eLine pipeline variant. So the debug branch must be None AND wireframe true.
    {
        const ResolvedSkinnedShading r = resolveSkinnedShading(5);
        CHECK(r.debugMode == SkinnedDebugMode::None);
        CHECK(r.wireframe == true);
    }
}

TEST_CASE("resolveSkinnedShading: out-of-range selection falls back to the unchanged Lit mode")
{
    for (uint8_t bad : {uint8_t{6}, uint8_t{42}, uint8_t{255}})
    {
        const ResolvedSkinnedShading r = resolveSkinnedShading(bad);
        CHECK(r.debugMode == SkinnedDebugMode::None);
        CHECK(r.wireframe == false);
    }
}

TEST_CASE("keyLightDirectionFromSpherical: returns a unit vector at known angles")
{
    // azimuth 0, elevation 0 -> straight along +Z (the convention: cos(el)*sin(az)=0, sin(el)=0,
    // cos(el)*cos(az)=1).
    {
        const glm::vec3 d = keyLightDirectionFromSpherical(0.0f, 0.0f);
        CHECK(vecNear(d, glm::vec3(0.0f, 0.0f, 1.0f)));
        CHECK(near1(glm::length(d), 1.0f));
    }
    // elevation +90deg -> straight up (+Y), independent of azimuth.
    {
        const float halfPi = 1.57079632679f;
        const glm::vec3 d = keyLightDirectionFromSpherical(1.234f, halfPi);
        CHECK(vecNear(d, glm::vec3(0.0f, 1.0f, 0.0f), 1e-4f));
        CHECK(near1(glm::length(d), 1.0f, 1e-4f));
    }
    // azimuth +90deg, elevation 0 -> +X.
    {
        const float halfPi = 1.57079632679f;
        const glm::vec3 d = keyLightDirectionFromSpherical(halfPi, 0.0f);
        CHECK(vecNear(d, glm::vec3(1.0f, 0.0f, 0.0f), 1e-4f));
    }
}

TEST_CASE("keyLightDirectionFromSpherical: always normalized across a sweep")
{
    for (float az = -3.0f; az <= 3.0f; az += 0.37f)
    {
        for (float el = -1.5f; el <= 1.5f; el += 0.31f)
        {
            const glm::vec3 d = keyLightDirectionFromSpherical(az, el);
            CHECK(near1(glm::length(d), 1.0f, 1e-4f));
        }
    }
}

TEST_CASE("SkinnedMeshRenderData defaults to the no-op (today's output) state")
{
    // AnimatedMeshPreviewController constructs this and never sets the Phase-3 fields, so the
    // defaults MUST be the unchanged path: no debug branch, IBL-only lighting, fill pipeline.
    SkinnedMeshRenderData rd;
    CHECK(rd.debugMode == static_cast<uint32_t>(SkinnedDebugMode::None));
    CHECK(rd.lightingMode == static_cast<uint32_t>(SkinnedLightingMode::IBLOnly));
    CHECK(rd.wireframe == false);
}

TEST_CASE("SkinnedMeshPushConstants keeps std430 byte parity with the GLSL push-constant block")
{
    // These mirror the compile-time static_asserts in SkinnedMeshTypes.hpp; duplicated as runtime
    // checks so a layout regression is caught even if someone weakens the asserts.
    CHECK(sizeof(SkinnedMeshPushConstants) == 144);
    CHECK(offsetof(SkinnedMeshPushConstants, textureIndicesPacked) == 96);
    CHECK(offsetof(SkinnedMeshPushConstants, debugMode) == 112);
    CHECK(offsetof(SkinnedMeshPushConstants, lightingMode) == 116);
    CHECK(offsetof(SkinnedMeshPushConstants, keyLightDirIntensity) == 128);
}
