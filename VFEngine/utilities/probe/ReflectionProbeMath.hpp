#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// VK-1577 — Reflection probe influence + parallax math.
//
// Pure, allocation-free, Vulkan-free (same shape as SunTransmittance.hpp / SunEntitySync.hpp /
// AmbientCaptureScheduler.hpp), so every rule below is doctestable on the CPU. The GLSL in
// resources/shaders/common/reflection_probes.glsl is a line-for-line transcription of these
// functions — when one changes, the other MUST change with it, and the doctests are what keeps
// them honest.
//
// Conventions
// -----------
// * "Local space" is the probe's UNIT BOX: the entity's world transform with each axis divided by
//   halfExtents, so the bounds surface is exactly |x| = |y| = |z| = 1. This mirrors how
//   FogVolumeBufferManager builds its fog-volume matrix.
// * `blendDistance` is in WORLD units, measured INWARD from the bounds surface. Because local space
//   is normalized per axis, it converts to a DIFFERENT normalized distance on each axis — hence
//   blendNormalized is a vec3, not a scalar. Getting this wrong makes the falloff visibly
//   asymmetric on non-cubic probes.
// * Weights are 1 in the probe core and ramp to 0 at the bounds surface, so a caller can blend
//   probes front-to-back and let any unclaimed weight fall through to the global IBL.
namespace render::probe
{
    // Component-wise reciprocal that never divides by zero. Preserves sign so the slab test below
    // still picks the correct side when a ray is exactly axis-parallel (the resulting huge t is
    // then discarded by the min() across axes).
    [[nodiscard]] inline glm::vec3 safeInverse(const glm::vec3& v, float epsilon = 1e-6f)
    {
        auto guard = [epsilon](float c)
        {
            if (c > -epsilon && c < epsilon)
                return (c < 0.0f ? -epsilon : epsilon);
            return c;
        };
        return glm::vec3(1.0f / guard(v.x), 1.0f / guard(v.y), 1.0f / guard(v.z));
    }

    // World -> unit box. Mirrors FogVolumeBufferManager's construction:
    // scale(1/halfExtents) * inverse(worldMatrix).
    [[nodiscard]] inline glm::mat4 buildWorldToLocal(const glm::mat4& worldMatrix,
                                                     const glm::vec3& halfExtents)
    {
        const glm::vec3 he = glm::max(halfExtents, glm::vec3(1e-4f));
        glm::mat4 scaleToUnit(1.0f);
        scaleToUnit[0][0] = 1.0f / he.x;
        scaleToUnit[1][1] = 1.0f / he.y;
        scaleToUnit[2][2] = 1.0f / he.z;
        return scaleToUnit * glm::inverse(worldMatrix);
    }

    // Unit box -> world. Stored alongside worldToLocal in the GPU struct so the shader never has to
    // call inverse() per fragment.
    [[nodiscard]] inline glm::mat4 buildLocalToWorld(const glm::mat4& worldMatrix,
                                                     const glm::vec3& halfExtents)
    {
        const glm::vec3 he = glm::max(halfExtents, glm::vec3(1e-4f));
        glm::mat4 scaleFromUnit(1.0f);
        scaleFromUnit[0][0] = he.x;
        scaleFromUnit[1][1] = he.y;
        scaleFromUnit[2][2] = he.z;
        return worldMatrix * scaleFromUnit;
    }

    // Per-axis normalized blend band. blendDistance is world units inward from the surface; dividing
    // by the half-extent expresses it in the unit box's coordinates.
    [[nodiscard]] inline glm::vec3 blendNormalized(const glm::vec3& halfExtents, float blendDistance)
    {
        const glm::vec3 he = glm::max(halfExtents, glm::vec3(1e-4f));
        return glm::max(glm::vec3(blendDistance) / he, glm::vec3(1e-4f));
    }

    // Hermite smoothstep on an already-clamped [0,1] ratio.
    //
    // The falloff is deliberately NOT linear. A linear ramp is only C0: its derivative jumps at both
    // ends of the blend band, and on a large smooth surface crossing a probe boundary that shows up
    // as a visible crease line. smoothstep is C1 at both ends, so the hand-off to the global
    // environment has no detectable edge. Costs two multiplies.
    [[nodiscard]] inline float smoothFalloff(float t)
    {
        return t * t * (3.0f - 2.0f * t);
    }

    // Box influence weight for a fragment already transformed into the probe's unit box.
    // 1 in the core, smoothly ramping to 0 on the bounds surface, 0 outside.
    //
    // The ratio is computed PER AXIS against that axis' own normalized band (blendNorm is a vec3),
    // then min()'d. Two consequences, both intended: the band stays a constant number of WORLD units
    // on every axis even when the probe is 5x longer in X than in Y, and a fragment near ANY face
    // fades — so corners, where two faces are close at once, fade on the nearer one rather than
    // popping. A single Chebyshev distance against one scalar band would get the non-cubic case
    // wrong (the band would be wider in world units on the long axis).
    [[nodiscard]] inline float probeWeightBox(const glm::vec3& pLocal, const glm::vec3& blendNorm)
    {
        const glm::vec3 dist = glm::vec3(1.0f) - glm::abs(pLocal); // >0 inside, <=0 outside
        const glm::vec3 w = dist / glm::max(blendNorm, glm::vec3(1e-4f));
        const float t = std::clamp(std::min(w.x, std::min(w.y, w.z)), 0.0f, 1.0f);
        return smoothFalloff(t);
    }

    // Sphere influence weight. `radius` is the probe's world-space radius.
    [[nodiscard]] inline float probeWeightSphere(const glm::vec3& fragWorld,
                                                 const glm::vec3& center,
                                                 float radius,
                                                 float blendDistance)
    {
        const float d = glm::length(fragWorld - center);
        const float t = std::clamp((radius - d) / std::max(blendDistance, 1e-4f), 0.0f, 1.0f);
        return smoothFalloff(t);
    }

    // Parallax-corrected reflection direction for a BOX probe (Lagarde box projection).
    //
    // A plain cubemap lookup assumes the environment is infinitely far away, so a reflection only
    // depends on the direction — walk a mirror toward a wall and its reflection never changes.
    // Box projection fixes that: intersect the reflection ray with the probe's bounds, then look up
    // the direction from the probe's CAPTURE POINT to that hit, which is where the captured texel
    // actually lives.
    //
    // CORRECTNESS NOTE — why the non-orthonormal mat3 is safe here.
    // `worldToLocal` folds 1/halfExtents in, so for a non-cubic probe mat3(worldToLocal) is NOT
    // orthonormal and `rLocal` comes out sheared and non-unit-length. That is fine, and deliberately
    // so: ray/box intersection is AFFINE-INVARIANT — an affine map preserves the ray parameter `t`,
    // so solving against the fixed unit cube yields the same `t` as solving against the real OBB in
    // world space. Two rules make it hold, and both are load-bearing:
    //   1. NEVER normalize rLocal. Normalizing rescales the ray parameter and silently breaks the
    //      correspondence — this is the classic bug in this formulation.
    //   2. Reconstruct the hit in LOCAL space, then map it back through localToWorld. Applying a
    //      local `t` to the world-space R would be wrong whenever the axes scale differently.
    // test_reflection_probe_math.cpp pins this down with non-cubic and rotated+non-cubic cases.
    //
    // Returns a normalized world-space direction suitable for sampling the probe cubemap.
    [[nodiscard]] inline glm::vec3 parallaxCorrectBox(const glm::vec3& R,
                                                      const glm::vec3& fragWorld,
                                                      const glm::mat4& worldToLocal,
                                                      const glm::mat4& localToWorld,
                                                      const glm::vec3& capturePos)
    {
        const glm::vec3 pLocal = glm::vec3(worldToLocal * glm::vec4(fragWorld, 1.0f));
        const glm::vec3 rLocal = glm::mat3(worldToLocal) * R;

        // Slab test against the unit cube; max() per axis picks each slab's EXIT t, min() across
        // axes picks the first face the ray actually leaves through.
        const glm::vec3 invR = safeInverse(rLocal);
        const glm::vec3 tMax = (glm::vec3(1.0f) - pLocal) * invR;
        const glm::vec3 tMin = (glm::vec3(-1.0f) - pLocal) * invR;
        const glm::vec3 t = glm::max(tMax, tMin);
        const float dist = std::min(t.x, std::min(t.y, t.z));

        const glm::vec3 hitLocal = pLocal + rLocal * dist;
        const glm::vec3 hitWorld = glm::vec3(localToWorld * glm::vec4(hitLocal, 1.0f));

        const glm::vec3 dir = hitWorld - capturePos;
        const float len = glm::length(dir);
        // Degenerate only when the hit lands exactly on the capture point; fall back to the
        // uncorrected direction rather than emitting a NaN.
        return len > 1e-6f ? dir / len : R;
    }

    // Parallax-corrected reflection direction for a SPHERE probe. Takes the FAR intersection of the
    // ray with the bounding sphere (the point the ray exits through), matching the box case.
    [[nodiscard]] inline glm::vec3 parallaxCorrectSphere(const glm::vec3& R,
                                                         const glm::vec3& fragWorld,
                                                         const glm::vec3& center,
                                                         float radius,
                                                         const glm::vec3& capturePos)
    {
        const glm::vec3 oc = fragWorld - center;
        const float b = glm::dot(oc, R);
        const float c = glm::dot(oc, oc) - radius * radius;
        const float disc = b * b - c;
        if (disc <= 0.0f)
            return R; // fragment outside the sphere and looking away — nothing to correct against

        const float t = -b + std::sqrt(disc);
        const glm::vec3 hitWorld = fragWorld + R * t;

        const glm::vec3 dir = hitWorld - capturePos;
        const float len = glm::length(dir);
        return len > 1e-6f ? dir / len : R;
    }

    // World-space AABB of an oriented box, used for cheap CPU-side rejection before the exact test.
    struct WorldBounds
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    [[nodiscard]] inline WorldBounds worldBoundsOfBox(const glm::mat4& worldMatrix,
                                                      const glm::vec3& halfExtents)
    {
        const glm::vec3 h = halfExtents;
        const glm::vec3 corners[8] = {
            {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z},
            {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {h.x, h.y, h.z}
        };

        WorldBounds out{glm::vec3(std::numeric_limits<float>::max()),
                        glm::vec3(std::numeric_limits<float>::lowest())};
        for (const auto& corner : corners)
        {
            const glm::vec3 w = glm::vec3(worldMatrix * glm::vec4(corner, 1.0f));
            out.min = glm::min(out.min, w);
            out.max = glm::max(out.max, w);
        }
        return out;
    }

    // Ordering key for probe upload. The GPU blend loop consumes probes in array order and clamps
    // each weight to the REMAINING budget, so the order is not cosmetic — it decides the result
    // wherever probes overlap. Two probes swapping places between frames is a visible flicker.
    //
    // Ordering: higher `priority` first, then the SMALLER volume first (a small probe nested inside
    // a big one is the more specific answer — the standard "smallest wins" heuristic), then the
    // entity UUID.
    //
    // The UUID tiebreak is REQUIRED, not decorative. `registry.view<>()` iteration order is an EnTT
    // pool detail: it shifts as components are added and removed, and it is not preserved across
    // save/load. Using the enumeration index as the tiebreak would make two same-priority,
    // same-size probes swap order on an unrelated edit. The UUID is stable for the entity's life
    // and survives serialization, so the order is reproducible.
    //
    // Note also what is NOT in the key: camera distance. It is the obvious reach and it is wrong —
    // a view-dependent key re-sorts as the camera moves, and every reorder is a discontinuity in the
    // blend. Every key here is view-independent, which makes temporal stability structural rather
    // than something that has to be damped later.
    struct ProbeSortKey
    {
        int32_t priority = 0;
        float volume = 0.0f;
        uint64_t stableId = 0; // entity UUID (uuid::UUID::getValue()); total, reproducible order
    };

    [[nodiscard]] inline bool probeSortLess(const ProbeSortKey& a, const ProbeSortKey& b)
    {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.volume != b.volume) return a.volume < b.volume;
        return a.stableId < b.stableId;
    }

    // Volume used by the sort. Box: the true box volume. Sphere: the sphere volume from radius (.x).
    [[nodiscard]] inline float probeVolume(const glm::vec3& halfExtents, bool isSphere)
    {
        if (isSphere)
        {
            const float r = std::max(halfExtents.x, 1e-4f);
            return (4.0f / 3.0f) * 3.14159265358979f * r * r * r;
        }
        const glm::vec3 he = glm::max(halfExtents, glm::vec3(1e-4f));
        return 8.0f * he.x * he.y * he.z;
    }
}
