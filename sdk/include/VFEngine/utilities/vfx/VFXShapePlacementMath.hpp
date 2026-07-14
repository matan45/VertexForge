#pragma once

#include "VFXShapeTypes.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

// VK-1525 — CPU mirror of the ordered / path-driven spawn-placement math.
//
// The GLSL file resources/shaders/vfx/vfx_shape_placement.glsl is the SOURCE OF
// TRUTH (it runs on the GPU sim; the editor CPU-preview VFXParticleSystem calls
// this header). Keep the two in LOCKSTEP — mirrors the VK-1476
// VFXOrientationMath.hpp <-> vfx_mesh_orientation.glsl pattern.
//
// "Ordered" placement makes a spawn position a deterministic function of the
// emitter's normalized age `progress` in [0,1], so a shape "draws itself out"
// one particle after another instead of filling randomly. `progress` is derived
// from the emitter's monotonic emission time (deterministic under sequence
// seek/prewarm) via vfxspOrderedProgress().
//
// The RNG (vfxspHash/vfxspRandomFloat) is a bit-exact mirror of pcg_hash/randomFloat
// in vfx_particle_sim.glsl; the `vfxsp` prefix keeps it from clashing with the sim's
// identifiers or VFXOrientationMath's `vfxo` helpers.
namespace vfx
{
    inline constexpr float VFXSP_TWO_PI = 6.28318530718f;

    // Spiral density for the shapes that sweep an angle as they draw out. Aesthetic
    // constants (not authored per-emitter); tune here and in the GLSL mirror together.
    inline constexpr float VFXSP_CONE_TURNS = 2.0f;
    inline constexpr float VFXSP_SPHERE_TURNS = 4.0f;
    inline constexpr float VFXSP_TORUS_COILS = 6.0f;

    // --- RNG primitives (bit-exact mirror of vfx_particle_sim.glsl) --------------

    inline uint32_t vfxspHash(uint32_t v)
    {
        uint32_t state = v * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    // Advances `seed` and returns a float in [0,1]. Matches GLSL vfxspRandomFloat.
    inline float vfxspRandomFloat(uint32_t& seed)
    {
        seed = vfxspHash(seed);
        return static_cast<float>(seed) / static_cast<float>(0xFFFFFFFFu);
    }

    // Deterministic per-particle jitter seed for ordered placement — the exact mirror
    // of the inline derivation in vfx_particle_sim.glsl / vfx_shape_placement.glsl, so
    // the CPU preview and the GPU sim scatter ordered particles identically. Keyed on
    // the emitter seed, the quantized sweep progress, and the particle's spawn slot
    // (its index within the frame's spawn batch), it reproduces under seek/prewarm —
    // unlike drawing from a running RNG stream. Truncation of progress*65535 matches
    // GLSL uint(float) (progress is in [0,1], so it is always non-negative).
    inline uint32_t vfxspOrderedJitterSeed(uint32_t emitterSeed, float progress, uint32_t spawnSlot)
    {
        return vfxspHash(emitterSeed ^ vfxspHash(static_cast<uint32_t>(progress * 65535.0f) ^ spawnSlot));
    }

    // --- Sweep parameter (host-side; the GPU consumes the uploaded result) -------

    // Normalized sweep parameter for the emitter's current age. `emitterAge` is the
    // monotonic emission time (set directly by seek, reproduced by prewarm re-sim), so
    // this is deterministic. One-shot clamps to [0,1] ("form once"); loop wraps via fract.
    inline float vfxspOrderedProgress(float emitterAge, float drawDuration, bool loop)
    {
        float dur = (drawDuration > 1e-4f) ? drawDuration : 1e-4f;
        float raw = emitterAge / dur;
        if (loop)
            return raw - std::floor(raw);
        return std::clamp(raw, 0.0f, 1.0f);
    }

    // --- Ordered placement -------------------------------------------------------

    // On-curve point for `progress` in [0,1], per shape. `dims` follows the same packing
    // as ShapeConfig::dimensions (Ring: x=radius, y=thickness, z=arcSpan, w=startAngle).
    inline glm::vec3 vfxspOrderedCurve(ShapeType type, const glm::vec4& dims, float progress)
    {
        switch (type)
        {
        case ShapeType::Ring:
        {
            float theta = dims.w + progress * dims.z;
            float radius = dims.x;
            return glm::vec3(radius * std::cos(theta), 0.0f, radius * std::sin(theta));
        }
        case ShapeType::Torus:
        {
            float theta = progress * VFXSP_TWO_PI;
            float minorAngle = progress * VFXSP_TWO_PI * VFXSP_TORUS_COILS;
            float ringDist = dims.x + dims.y * std::cos(minorAngle);
            return glm::vec3(ringDist * std::cos(theta), dims.y * std::sin(minorAngle), ringDist * std::sin(theta));
        }
        case ShapeType::Cone:
        {
            float y = progress * dims.y;
            float r = progress * dims.x * std::tan(dims.z);
            float theta = progress * VFXSP_TWO_PI * VFXSP_CONE_TURNS;
            return glm::vec3(r * std::cos(theta), y, r * std::sin(theta));
        }
        case ShapeType::Sphere:
        {
            float radius = dims.x;
            float y = radius * (2.0f * progress - 1.0f);
            float ringR = std::sqrt(std::max(0.0f, radius * radius - y * y));
            float theta = progress * VFXSP_TWO_PI * VFXSP_SPHERE_TURNS;
            return glm::vec3(ringR * std::cos(theta), y, ringR * std::sin(theta));
        }
        case ShapeType::Box:
        {
            glm::vec3 half3(dims);
            return glm::mix(-half3, half3, progress);
        }
        case ShapeType::Point:
        default:
            return glm::vec3(0.0f);
        }
    }

    // Ordered placement = on-curve point + optional deterministic per-particle jitter.
    // `jitterAmt` == 0 returns the exact on-curve point (the determinism-demo default).
    inline glm::vec3 vfxspOrderedPosition(ShapeType type, const glm::vec4& dims, float progress,
                                          float jitterAmt, uint32_t jitterSeed)
    {
        glm::vec3 pos = vfxspOrderedCurve(type, dims, progress);
        if (jitterAmt > 0.0f)
        {
            uint32_t s = jitterSeed;
            float jx = vfxspRandomFloat(s) * 2.0f - 1.0f;
            float jy = vfxspRandomFloat(s) * 2.0f - 1.0f;
            float jz = vfxspRandomFloat(s) * 2.0f - 1.0f;
            pos += jitterAmt * glm::vec3(jx, jy, jz);
        }
        return pos;
    }
}
