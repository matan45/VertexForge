#include <doctest.h>

// ============================================================
// VK-1525: ordered / path-driven spawn placement + Ring shape.
//
// These pure helpers (VFXShapePlacementMath.hpp) are the CPU spec
// for the GPU placement math in resources/shaders/vfx/
// vfx_shape_placement.glsl — keep the two in lockstep. Ordered
// placement makes a spawn position a deterministic function of the
// emitter's normalized age `progress`, so a shape "draws itself
// out" and replays identically under sequence seek/prewarm.
//
// Also locks down the reclaimed-pad ABI (sizeof stays 512/176/80,
// the three reclaimed floats keep offsets 332/376/380) and the new
// ShapeFlags bits, so the C++ struct and the GLSL mirror can't drift.
// ============================================================

#include <vfx/VFXShapePlacementMath.hpp>
#include <vfx/VFXShapeTypes.hpp>
#include "render/vfx/compute/GPUVFXTypes.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <cstddef>

using vfx::ShapeType;
using vfx::vfxspOrderedCurve;
using vfx::vfxspOrderedPosition;
using vfx::vfxspOrderedProgress;

namespace
{
    constexpr float kTwoPi = 6.28318530718f;

    void checkVecApprox(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
        CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
        CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
    }

    glm::vec4 ringDims(float radius, float thickness, float arc, float startAngle)
    {
        return glm::vec4(radius, thickness, arc, startAngle);
    }
}

// ---------------------------------------------------------------------------
// ABI: reclaimed pads keep the struct at 512 bytes and preserve every offset.
// ---------------------------------------------------------------------------
TEST_CASE("VFXShapePlacement: GPU struct ABI is unchanged (reclaimed pads)")
{
    using render::vfx::GPUEmitterConfig;
    using render::vfx::GPUEmitterState;
    using render::vfx::GPUParticle;

    CHECK(sizeof(GPUEmitterConfig) == 512);
    CHECK(sizeof(GPUEmitterState) == 176);
    CHECK(sizeof(GPUParticle) == 80);

    // The three reclaimed pad floats keep their original offsets.
    CHECK(offsetof(GPUEmitterConfig, orderedSweepTPrev) == 332);
    CHECK(offsetof(GPUEmitterConfig, orderedJitter) == 376);
    CHECK(offsetof(GPUEmitterConfig, orderedSweepT) == 380);

    // shapeFlags is a separate uint32 from modifierFlags; the new bits live at 14/15.
    CHECK(render::vfx::ShapeFlags::ShapeRing == (1u << 14));
    CHECK(render::vfx::ShapeFlags::OrderedPlacement == (1u << 15));
    // Torus keeps bit 11 (now a real 3-D torus, no longer a flat circle).
    CHECK(render::vfx::ShapeFlags::ShapeTorus == (1u << 11));
}

TEST_CASE("VFXShapePlacement: ShapeType::Ring appends without renumbering")
{
    CHECK(static_cast<int>(ShapeType::Point) == 0);
    CHECK(static_cast<int>(ShapeType::Torus) == 4);
    CHECK(static_cast<int>(ShapeType::Ring) == 5);
    CHECK(vfx::stringToShapeType("Ring") == ShapeType::Ring);
    CHECK(std::string(vfx::shapeTypeToString(ShapeType::Ring)) == "Ring");
}

// ---------------------------------------------------------------------------
// Sweep parameter: one-shot clamps, loop wraps, guarded against zero duration.
// ---------------------------------------------------------------------------
TEST_CASE("VFXShapePlacement: ordered progress (one-shot vs loop)")
{
    const float dur = 2.0f;

    // One-shot clamps to [0,1].
    CHECK(vfxspOrderedProgress(0.0f, dur, false) == doctest::Approx(0.0f));
    CHECK(vfxspOrderedProgress(1.0f, dur, false) == doctest::Approx(0.5f));
    CHECK(vfxspOrderedProgress(dur, dur, false) == doctest::Approx(1.0f));
    CHECK(vfxspOrderedProgress(3.0f * dur, dur, false) == doctest::Approx(1.0f)); // past the end

    // Loop wraps via fract.
    CHECK(vfxspOrderedProgress(0.5f * dur, dur, true) == doctest::Approx(0.5f));
    CHECK(vfxspOrderedProgress(1.5f * dur, dur, true) == doctest::Approx(0.5f));
    CHECK(vfxspOrderedProgress(2.0f * dur, dur, true) == doctest::Approx(0.0f));

    // Zero / negative duration is guarded (finite, never a divide-by-zero).
    CHECK(std::isfinite(vfxspOrderedProgress(1.0f, 0.0f, false)));
    CHECK(vfxspOrderedProgress(1.0f, 0.0f, false) == doctest::Approx(1.0f)); // huge ratio -> clamped
}

// ---------------------------------------------------------------------------
// Core AC: placement is a pure function of age, so "seek" == "prewarm".
// ---------------------------------------------------------------------------
TEST_CASE("VFXShapePlacement: deterministic under seek vs prewarm (frame-path independent)")
{
    const glm::vec4 dims = ringDims(1.5f, 0.0f, kTwoPi, 0.0f);
    const float dur = 2.0f;
    const float target = 1.3f;

    // "Seek": evaluate age directly at the target.
    float pSeek = vfxspOrderedProgress(target, dur, false);
    glm::vec3 posSeek = vfxspOrderedPosition(ShapeType::Ring, dims, pSeek, 0.0f, 7u);

    // "Prewarm": accumulate the same age in N fixed sub-steps.
    float age = 0.0f;
    const int steps = 32;
    const float dt = target / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i)
        age += dt;
    float pWarm = vfxspOrderedProgress(age, dur, false);
    glm::vec3 posWarm = vfxspOrderedPosition(ShapeType::Ring, dims, pWarm, 0.0f, 7u);

    checkVecApprox(posSeek, posWarm, 1e-4f);

    // Same inputs -> byte-identical repeat (pure function, jitter included).
    glm::vec3 a = vfxspOrderedPosition(ShapeType::Ring, dims, 0.42f, 0.5f, 99u);
    glm::vec3 b = vfxspOrderedPosition(ShapeType::Ring, dims, 0.42f, 0.5f, 99u);
    CHECK(a == b);
}

// ---------------------------------------------------------------------------
// Ring: monotonic arc sweep, correct geometry and endpoints.
// ---------------------------------------------------------------------------
TEST_CASE("VFXShapePlacement: Ring draws the arc out in order")
{
    const float radius = 2.0f;
    const float arc = kTwoPi * 0.5f;      // 180-degree arc
    const float start = 0.0f;
    const glm::vec4 dims = ringDims(radius, 0.0f, arc, start);

    // Endpoints: progress 0 -> startAngle, progress 1 -> startAngle + arc.
    checkVecApprox(vfxspOrderedCurve(ShapeType::Ring, dims, 0.0f), glm::vec3(radius, 0.0f, 0.0f));
    checkVecApprox(vfxspOrderedCurve(ShapeType::Ring, dims, 0.5f), glm::vec3(0.0f, 0.0f, radius));
    checkVecApprox(vfxspOrderedCurve(ShapeType::Ring, dims, 1.0f), glm::vec3(-radius, 0.0f, 0.0f));

    // On the ring (r == radius, y == 0) and the swept angle is monotonic non-decreasing.
    float prevAngle = -1e9f;
    for (int i = 0; i <= 16; ++i)
    {
        float t = static_cast<float>(i) / 16.0f;
        glm::vec3 p = vfxspOrderedCurve(ShapeType::Ring, dims, t);
        CHECK(std::sqrt(p.x * p.x + p.z * p.z) == doctest::Approx(radius).epsilon(1e-4f));
        CHECK(p.y == doctest::Approx(0.0f));
        float angle = std::atan2(p.z, p.x); // within [0, pi] for this arc, so atan2 is monotonic here
        CHECK(angle >= prevAngle - 1e-4f);
        prevAngle = angle;
    }
}

TEST_CASE("VFXShapePlacement: Ring startAngle offsets the arc")
{
    const float radius = 1.0f;
    const float start = kTwoPi * 0.25f; // start a quarter turn in
    const glm::vec4 dims = ringDims(radius, 0.0f, kTwoPi * 0.5f, start);
    // progress 0 lands at the start angle (0, 0, radius).
    checkVecApprox(vfxspOrderedCurve(ShapeType::Ring, dims, 0.0f), glm::vec3(0.0f, 0.0f, radius));
}

// ---------------------------------------------------------------------------
// Torus fix: ordered points sit on the 3-D tube surface.
// ---------------------------------------------------------------------------
TEST_CASE("VFXShapePlacement: Torus ordered points lie on the 3-D tube")
{
    const float major = 2.0f;
    const float minor = 0.5f;
    const glm::vec4 dims(major, minor, 0.0f, 0.0f);

    for (int i = 0; i <= 20; ++i)
    {
        float t = static_cast<float>(i) / 20.0f;
        glm::vec3 p = vfxspOrderedCurve(ShapeType::Torus, dims, t);
        float ringDist = std::sqrt(p.x * p.x + p.z * p.z);
        // (sqrt(x^2+z^2) - major)^2 + y^2 == minor^2 for a point on the tube surface.
        float onTube = (ringDist - major) * (ringDist - major) + p.y * p.y;
        CHECK(onTube == doctest::Approx(minor * minor).epsilon(1e-3f));
    }
}

// ---------------------------------------------------------------------------
// Cone / Sphere / Box ordered endpoints.
// ---------------------------------------------------------------------------
TEST_CASE("VFXShapePlacement: Cone spirals from apex to rim")
{
    const float baseRadius = 1.0f;
    const float height = 3.0f;
    const float angle = 0.4f; // radians
    const glm::vec4 dims(baseRadius, height, angle, 0.0f);

    // Apex at progress 0.
    checkVecApprox(vfxspOrderedCurve(ShapeType::Cone, dims, 0.0f), glm::vec3(0.0f));

    // At progress 1: y == height, radial distance == baseRadius * tan(angle).
    glm::vec3 top = vfxspOrderedCurve(ShapeType::Cone, dims, 1.0f);
    CHECK(top.y == doctest::Approx(height));
    CHECK(std::sqrt(top.x * top.x + top.z * top.z) ==
          doctest::Approx(baseRadius * std::tan(angle)).epsilon(1e-4f));
}

TEST_CASE("VFXShapePlacement: Sphere spirals pole to pole")
{
    const float radius = 2.0f;
    const glm::vec4 dims(radius, 0.0f, 0.0f, 0.0f);

    // Bottom pole at progress 0, top pole at progress 1.
    checkVecApprox(vfxspOrderedCurve(ShapeType::Sphere, dims, 0.0f), glm::vec3(0.0f, -radius, 0.0f));
    checkVecApprox(vfxspOrderedCurve(ShapeType::Sphere, dims, 1.0f), glm::vec3(0.0f, radius, 0.0f));

    // Equator at progress 0.5: y == 0, on the sphere.
    glm::vec3 eq = vfxspOrderedCurve(ShapeType::Sphere, dims, 0.5f);
    CHECK(eq.y == doctest::Approx(0.0f));
    CHECK(glm::length(eq) == doctest::Approx(radius).epsilon(1e-4f));
}

TEST_CASE("VFXShapePlacement: Box marches along the diagonal")
{
    const glm::vec3 half(1.0f, 2.0f, 3.0f);
    const glm::vec4 dims(half, 0.0f);

    checkVecApprox(vfxspOrderedCurve(ShapeType::Box, dims, 0.0f), -half);
    checkVecApprox(vfxspOrderedCurve(ShapeType::Box, dims, 0.5f), glm::vec3(0.0f));
    checkVecApprox(vfxspOrderedCurve(ShapeType::Box, dims, 1.0f), half);
}

// ---------------------------------------------------------------------------
// Jitter gating: 0 => exactly on-curve; > 0 => bounded, reproducible, seed-varying.
// ---------------------------------------------------------------------------
TEST_CASE("VFXShapePlacement: jitter is gated, bounded, and deterministic")
{
    const glm::vec4 dims = ringDims(1.0f, 0.0f, kTwoPi, 0.0f);
    const float progress = 0.3f;
    glm::vec3 curve = vfxspOrderedCurve(ShapeType::Ring, dims, progress);

    // jitter == 0 => exactly on the curve (the determinism-demo default).
    glm::vec3 exact = vfxspOrderedPosition(ShapeType::Ring, dims, progress, 0.0f, 12345u);
    CHECK(exact == curve);

    // jitter > 0 => within the per-axis bound, reproducible for a fixed seed.
    const float amt = 0.25f;
    glm::vec3 j1 = vfxspOrderedPosition(ShapeType::Ring, dims, progress, amt, 12345u);
    glm::vec3 j2 = vfxspOrderedPosition(ShapeType::Ring, dims, progress, amt, 12345u);
    CHECK(j1 == j2); // reproducible
    glm::vec3 d = j1 - curve;
    CHECK(std::abs(d.x) <= amt + 1e-5f);
    CHECK(std::abs(d.y) <= amt + 1e-5f);
    CHECK(std::abs(d.z) <= amt + 1e-5f);

    // A different seed generally lands somewhere else.
    glm::vec3 j3 = vfxspOrderedPosition(ShapeType::Ring, dims, progress, amt, 999u);
    CHECK(j3 != j1);
}

// ---------------------------------------------------------------------------
// Legacy byte-identity: a default (non-ordered) config leaves the reclaimed
// floats zero and the new shapeFlags bits clear.
// ---------------------------------------------------------------------------
TEST_CASE("VFXShapePlacement: default config is byte-identical for legacy assets")
{
    render::vfx::GPUEmitterConfig cfg{}; // value-initialized (mirrors toGPUConfig)
    CHECK(cfg.orderedSweepT == 0.0f);
    CHECK(cfg.orderedSweepTPrev == 0.0f);
    CHECK(cfg.orderedJitter == 0.0f);
    CHECK((cfg.shapeFlags & render::vfx::ShapeFlags::ShapeRing) == 0u);
    CHECK((cfg.shapeFlags & render::vfx::ShapeFlags::OrderedPlacement) == 0u);

    // The CPU authoring struct defaults to ordered OFF.
    vfx::ShapeConfig shape;
    CHECK(shape.ordered == false);
    CHECK(shape.orderedLoop == false);
    CHECK(shape.sweepDuration == doctest::Approx(1.0f));
    CHECK(shape.orderedJitter == doctest::Approx(0.0f));
}
