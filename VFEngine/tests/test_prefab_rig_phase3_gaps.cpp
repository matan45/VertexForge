#include <doctest.h>

// VK-1433 Phase 3 (gaps) — additional CPU coverage for the debug-shading / analytic-lighting
// seams in render/mesh/SkinnedMeshTypes.hpp, complementing test_prefab_rig_phase3.cpp.
//
// Focus of THIS file (the documented gaps in the base suite):
//   - resolveSkinnedShading: the full 0..5 table re-asserted as an explicit (selection -> pair)
//     mapping, plus the enum-index contract (SkinnedShadingSelection values are exactly 0..5).
//   - keyLightDirectionFromSpherical: the cardinal directions the base suite omits (-Z at az=180,
//     -X at az=270, -Y at el=-90) and EXPLICIT finiteness at both poles.
//   - SkinnedMeshRenderData defaults: the FULL no-op state (keyLightDirection / lightingIntensity /
//     textureIndicesPacked all-NONE), and consistency with resolveSkinnedShading(0) — together the
//     CPU-side proof of the "AnimatedMeshPreview is byte-identical" guarantee.
//   - SkinnedMeshPushConstants: the std430 HEAD offsets + the pad offsets (120/124) the base suite
//     omits, and the C++ enum numeric values that MUST equal the GLSL debug/lighting constants
//     (DEBUG_*/LIGHTING_* in skinned_mesh.glsl) — the shared-shader regression guard.
//
// All pure / CPU-only. The shader output, wireframe pipeline, and three-point shaping are GPU-only
// and verified manually in-editor (see the deliverable notes).

#include "render/mesh/SkinnedMeshTypes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace render::mesh;

namespace
{
    bool nearG(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
    bool vecNearG(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return nearG(a.x, b.x, eps) && nearG(a.y, b.y, eps) && nearG(a.z, b.z, eps);
    }
}

// ---------------------------------------------------------------------------------------------
// resolveSkinnedShading — full table + the dropdown-index enum contract.
// ---------------------------------------------------------------------------------------------

TEST_CASE("resolveSkinnedShading: full 0..5 table maps to the exact (debugMode, wireframe) pair")
{
    struct Row { uint8_t sel; SkinnedDebugMode mode; bool wire; };
    // Derived directly from the switch in SkinnedMeshTypes.hpp (NOT from intuition).
    const Row table[] = {
        {0, SkinnedDebugMode::None,        false}, // Lit (default — preserves today's look)
        {1, SkinnedDebugMode::Clay,        false},
        {2, SkinnedDebugMode::Normals,     false},
        {3, SkinnedDebugMode::UVs,         false},
        {4, SkinnedDebugMode::AlbedoUnlit, false},
        {5, SkinnedDebugMode::None,        true},  // Wireframe: shader stays None, pipeline goes line
    };
    for (const Row& r : table)
    {
        CAPTURE(r.sel);
        const ResolvedSkinnedShading res = resolveSkinnedShading(r.sel);
        CHECK(res.debugMode == r.mode);
        CHECK(res.wireframe == r.wire);
    }
}

TEST_CASE("resolveSkinnedShading: Wireframe is the ONLY selection that sets wireframe=true")
{
    // Guards against a future edit accidentally flipping wireframe on a debug mode (which would
    // change the shader's effective output for that mode).
    for (uint8_t sel = 0; sel <= 5; ++sel)
    {
        CAPTURE(sel);
        const bool expectWire = (sel == static_cast<uint8_t>(SkinnedShadingSelection::Wireframe));
        CHECK(resolveSkinnedShading(sel).wireframe == expectWire);
    }
}

TEST_CASE("resolveSkinnedShading: out-of-range (incl. just-past-end and max) falls back to Lit")
{
    for (uint8_t bad : {uint8_t{6}, uint8_t{7}, uint8_t{100}, uint8_t{254}, uint8_t{255}})
    {
        CAPTURE(bad);
        const ResolvedSkinnedShading r = resolveSkinnedShading(bad);
        CHECK(r.debugMode == SkinnedDebugMode::None);
        CHECK(r.wireframe == false);
    }
}

TEST_CASE("SkinnedShadingSelection enum values are the contiguous dropdown indices 0..5")
{
    // The editor dropdown passes the selection's integer index across the env channel as a uint8;
    // resolveSkinnedShading reinterprets that index as this enum. If the enum ever reorders, the
    // dropdown-to-mode mapping silently breaks. Pin the contract.
    CHECK(static_cast<uint8_t>(SkinnedShadingSelection::Lit)         == 0);
    CHECK(static_cast<uint8_t>(SkinnedShadingSelection::Clay)        == 1);
    CHECK(static_cast<uint8_t>(SkinnedShadingSelection::Normals)     == 2);
    CHECK(static_cast<uint8_t>(SkinnedShadingSelection::UVs)         == 3);
    CHECK(static_cast<uint8_t>(SkinnedShadingSelection::AlbedoUnlit) == 4);
    CHECK(static_cast<uint8_t>(SkinnedShadingSelection::Wireframe)   == 5);
}

// ---------------------------------------------------------------------------------------------
// keyLightDirectionFromSpherical — the cardinal directions the base suite omits + pole finiteness.
// ---------------------------------------------------------------------------------------------

TEST_CASE("keyLightDirectionFromSpherical: the remaining cardinal directions")
{
    const float halfPi = glm::half_pi<float>();
    const float pi      = glm::pi<float>();

    // az=180, el=0 -> -Z  (sin(pi)=0, cos(pi)=-1).
    CHECK(vecNearG(keyLightDirectionFromSpherical(pi, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));
    // az=270 (=3pi/2), el=0 -> -X  (sin=-1, cos=0).
    CHECK(vecNearG(keyLightDirectionFromSpherical(3.0f * halfPi, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)));
    // el=-90 -> -Y, independent of azimuth (cos(el)=0).
    CHECK(vecNearG(keyLightDirectionFromSpherical(0.731f, -halfPi), glm::vec3(0.0f, -1.0f, 0.0f)));
    CHECK(vecNearG(keyLightDirectionFromSpherical(2.4f,   -halfPi), glm::vec3(0.0f, -1.0f, 0.0f)));
}

TEST_CASE("keyLightDirectionFromSpherical: unit-length and finite at both poles")
{
    const float halfPi = glm::half_pi<float>();
    for (float az : {-3.1f, 0.0f, 1.0f, 6.2f})
    {
        for (float el : {halfPi, -halfPi})
        {
            CAPTURE(az);
            CAPTURE(el);
            const glm::vec3 d = keyLightDirectionFromSpherical(az, el);
            CHECK(std::isfinite(d.x));
            CHECK(std::isfinite(d.y));
            CHECK(std::isfinite(d.z));
            CHECK(nearG(glm::length(d), 1.0f));
        }
    }
}

TEST_CASE("keyLightDirectionFromSpherical: azimuth wraps (az and az+2pi agree)")
{
    const float twoPi = glm::two_pi<float>();
    for (float az = -2.0f; az <= 2.0f; az += 0.5f)
    {
        for (float el = -1.0f; el <= 1.0f; el += 0.5f)
        {
            const glm::vec3 a = keyLightDirectionFromSpherical(az, el);
            const glm::vec3 b = keyLightDirectionFromSpherical(az + twoPi, el);
            CHECK(vecNearG(a, b));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// SkinnedMeshRenderData — the FULL default no-op state (the AnimatedMeshPreview-unchanged proof).
// ---------------------------------------------------------------------------------------------

TEST_CASE("SkinnedMeshRenderData: every Phase-3 field defaults to the no-op state")
{
    // AnimatedMeshPreviewController constructs this and sets ONLY albedo/metallic/roughness/ao/
    // emission/modelMatrix (verified in AnimatedMeshPreviewController.cpp). Every field below must
    // therefore default to the value that reproduces today's IBL-only, fill-pipeline output.
    SkinnedMeshRenderData rd;

    CHECK(rd.debugMode    == static_cast<uint32_t>(SkinnedDebugMode::None));
    CHECK(rd.lightingMode == static_cast<uint32_t>(SkinnedLightingMode::IBLOnly));
    CHECK(rd.wireframe    == false);
    CHECK(rd.lightingIntensity == doctest::Approx(1.0f));
    // keyLightDirection defaults to +Y; harmless because lightingMode=IBLOnly never reads it, but
    // it must still be a sane unit-ish value rather than zero (a zero dir + ThreePoint is the
    // shader's guarded early-out, not a default we want to depend on here).
    CHECK(vecNearG(rd.keyLightDirection, glm::vec3(0.0f, 1.0f, 0.0f)));

    // All-NONE texture indices => the fragment shader samples no material textures, the other
    // half of the byte-identical guarantee.
    for (uint32_t packed : rd.textureIndicesPacked)
        CHECK(packed == 0xFFFFFFFFu);
}

TEST_CASE("SkinnedMeshRenderData default is consistent with resolveSkinnedShading(Lit)")
{
    // The editor's default dropdown index is 0 (Lit). The resolver for 0 must agree with the
    // render-data default so a freshly-opened prefab preview and a never-configured caller render
    // identically.
    SkinnedMeshRenderData rd;
    const ResolvedSkinnedShading lit = resolveSkinnedShading(static_cast<uint8_t>(SkinnedShadingSelection::Lit));
    CHECK(static_cast<uint32_t>(lit.debugMode) == rd.debugMode);
    CHECK(lit.wireframe == rd.wireframe);
}

// ---------------------------------------------------------------------------------------------
// SkinnedMeshPushConstants — full std430 layout (head + pads + tail) and the shader-constant
// numeric contract. This is the shared-shader regression guard.
// ---------------------------------------------------------------------------------------------

TEST_CASE("SkinnedMeshPushConstants: full std430 head + pad + tail offsets and size")
{
    // HEAD — byte-identical to the original scalar-PBR push constant (offsets 0..96).
    CHECK(offsetof(SkinnedMeshPushConstants, model)     == 0);
    CHECK(offsetof(SkinnedMeshPushConstants, albedo)    == 64);
    CHECK(offsetof(SkinnedMeshPushConstants, metallic)  == 80);
    CHECK(offsetof(SkinnedMeshPushConstants, roughness) == 84);
    CHECK(offsetof(SkinnedMeshPushConstants, ao)        == 88);
    CHECK(offsetof(SkinnedMeshPushConstants, emission)  == 92);
    CHECK(offsetof(SkinnedMeshPushConstants, textureIndicesPacked) == 96);

    // PHASE-3 TAIL — must match the std430 push-constant block in BOTH GLSL stages of
    // skinned_mesh.glsl (debugMode@112, lightingMode@116, _pad0@120, _pad1@124, vec4@128).
    CHECK(offsetof(SkinnedMeshPushConstants, debugMode)    == 112);
    CHECK(offsetof(SkinnedMeshPushConstants, lightingMode) == 116);
    CHECK(offsetof(SkinnedMeshPushConstants, _pad0)        == 120);
    CHECK(offsetof(SkinnedMeshPushConstants, _pad1)        == 124);
    CHECK(offsetof(SkinnedMeshPushConstants, keyLightDirIntensity) == 128);

    CHECK(sizeof(SkinnedMeshPushConstants) == 144);

    // The keyLightDirIntensity vec4 must be 16-byte aligned (std430) — the two pads exist solely
    // to achieve that; if a future edit removes them the offset drifts off 128.
    CHECK(offsetof(SkinnedMeshPushConstants, keyLightDirIntensity) % 16 == 0);
}

TEST_CASE("SkinnedMeshPushConstants: default-constructed tail is the no-op state")
{
    // The struct's own member defaults (not the render path) must already be the no-op state, so a
    // value-initialized `SkinnedMeshPushConstants pc{};` (as recordCommandBuffer does) is safe even
    // before the render path assigns the tail.
    SkinnedMeshPushConstants pc{};
    CHECK(pc.debugMode == 0u);
    CHECK(pc.lightingMode == 0u);
    CHECK(pc.keyLightDirIntensity == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    for (uint32_t packed : pc.textureIndicesPacked)
        CHECK(packed == 0xFFFFFFFFu);
}

TEST_CASE("SkinnedDebugMode / SkinnedLightingMode numeric values match the GLSL shader constants")
{
    // skinned_mesh.glsl branches on literal uints: DEBUG_NONE=0, DEBUG_CLAY=1, DEBUG_NORMALS=2,
    // DEBUG_UVS=3, DEBUG_ALBEDO_UNLIT=4; LIGHTING_IBL_ONLY=0, LIGHTING_THREE_POINT=1. The C++ enum
    // underlying values are pushed verbatim, so they MUST equal those literals. If the enum drifts
    // from the shader, the wrong branch lights up at runtime (a silent, GPU-only failure) — this
    // pins the cross-language contract on the CPU side.
    CHECK(static_cast<uint32_t>(SkinnedDebugMode::None)        == 0u);
    CHECK(static_cast<uint32_t>(SkinnedDebugMode::Clay)        == 1u);
    CHECK(static_cast<uint32_t>(SkinnedDebugMode::Normals)     == 2u);
    CHECK(static_cast<uint32_t>(SkinnedDebugMode::UVs)         == 3u);
    CHECK(static_cast<uint32_t>(SkinnedDebugMode::AlbedoUnlit) == 4u);

    CHECK(static_cast<uint32_t>(SkinnedLightingMode::IBLOnly)    == 0u);
    CHECK(static_cast<uint32_t>(SkinnedLightingMode::ThreePoint) == 1u);
}
