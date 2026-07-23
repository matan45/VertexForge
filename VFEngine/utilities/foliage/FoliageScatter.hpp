#pragma once
// VK-1575: pure, Vulkan-free foliage scatter core. Generalizes the vegetation brush's
// candidate generation (VegetationBrushServiceImpl::generateAndPlaceCandidates) into a
// deterministic, dependency-light function so it can be exercised by the doctest Tests
// target and reused by the foliage brush (InstancedFoliage backend).
//
// The impure boundary — terrain height, surface normal, and the per-tile spacing grid —
// is injected through ScatterEnv callbacks, and the candidate output is neutral
// (ScatterCandidate) so the caller's emit adapter maps it to a foliage::FoliageInstance.
// No EventDispatcher, no Vulkan, no mesh DB here.
#include "../terrain/BrushFalloff.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <vector>

namespace foliage
{
    // Neutral per-candidate result. The emit adapter maps this to a FoliageInstance
    // (positions/rotationY/scale/normal are already final; no AABB Y-offset is applied here).
    struct ScatterCandidate
    {
        glm::vec3 position{0.0f};             // terrain-seated world position (heightAt applied)
        glm::vec3 normal{0.0f, 1.0f, 0.0f};   // final normal (surface normal + tilt, or up)
        float     rotationY = 0.0f;           // Y rotation, radians
        glm::vec3 scale{1.0f};                // (s, s*heightMult, s)
        uint16_t  typeIndex = 0;              // palette index (== index into the caller's rules)
        bool      tilted    = false;          // random tilt applied -> FoliageInstanceFlags::Tilt
        float     windPhase = 0.0f;           // [0,1]
        uint32_t  seed      = 0;              // deterministic per-instance seed
    };

    // Neutral placement rule. Each caller adapts its own palette entry (foliage::FoliageType
    // or meshbrush::MeshPaletteEntry) to this so the core depends on neither. Angles in degrees.
    struct ScatterTypeRule
    {
        float     weight = 1.0f;                    // weighted random pick
        glm::vec2 scaleRange{0.8f, 1.2f};           // uniform scale min/max
        glm::vec2 heightRange{1.0f, 1.0f};          // extra Y multiplier min/max
        glm::vec2 rotationYRangeDeg{0.0f, 360.0f};
        float     randomTiltDeg = 0.0f;             // max random tilt off surface normal (needs alignToNormal)
        bool      alignToNormal = false;
        float     minSlopeDeg = 0.0f;
        float     maxSlopeDeg = 90.0f;
        glm::vec2 altitudeRange{-100000.0f, 100000.0f}; // world-Y min/max
    };

    struct ScatterParams
    {
        float                 radius = 5.0f;
        float                 spacing = 2.0f;
        float                 positionJitter = 0.5f;
        uint32_t              maxCandidates = 4096;                      // foliage 4096, actor 100
        terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth;
        bool                  applyFalloff = true;   // false => SKIP the falloff rng draw (mesh-brush parity)
        bool                  perCandidateNormal = true; // false => use fixedNormal for every candidate
    };

    struct ScatterEnv
    {
        // Terrain height at (x,z). std::nullopt => off-terrain / invalid, candidate rejected.
        std::function<std::optional<float>(float x, float z)> heightAt;
        // Surface normal at (x,z) (finite-difference). Only called when a picked rule needs it.
        std::function<glm::vec3(float x, float z)>            normalAt;
        // True => too close to an existing instance, reject.
        std::function<bool(const glm::vec3& pos)>             spacingReject;
        // Record an accepted position into the caller's spatial grid.
        std::function<void(const glm::vec3& pos, uint16_t typeIndex)> accept;
    };

    // Deterministic in `rng`. Appends accepted candidates to `out`; returns the count appended.
    // `enabledIndices` are indices into `rules` that are paint-enabled + non-empty (weighted pick).
    // `fixedNormal` is used when perCandidateNormal == false (mesh brush uses the brush-center normal).
    inline uint32_t scatterFoliage(const glm::vec3& brushCenter,
                                   const std::vector<ScatterTypeRule>& rules,
                                   const std::vector<uint32_t>& enabledIndices,
                                   const ScatterParams& params,
                                   const ScatterEnv& env,
                                   std::mt19937& rng,
                                   glm::vec3 fixedNormal,
                                   std::vector<ScatterCandidate>& out)
    {
        if (enabledIndices.empty() || !env.heightAt) return 0;

        std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> jitterDist(-0.5f, 0.5f);
        std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);

        // Weighted palette pick over enabledIndices (mirrors vegetation's discrete_distribution).
        std::vector<double> weights;
        weights.reserve(enabledIndices.size());
        for (uint32_t idx : enabledIndices)
            weights.push_back(static_cast<double>(std::max(rules[idx].weight, 0.0f)));
        std::discrete_distribution<size_t> paletteDist(weights.begin(), weights.end());

        const float radiusSafe = std::max(params.radius, 0.001f);
        uint32_t placed = 0;

        for (uint32_t c = 0; c < params.maxCandidates; ++c)
        {
            // Uniform disk sample.
            const float angle = angleDist(rng);
            const float r = params.radius * std::sqrt(radiusDist(rng));

            // Density falloff: thin toward the brush edge (before the terrain query so
            // rejected candidates stay cheap). Constant == no thinning; skipped entirely
            // when applyFalloff is false so the rng sequence stays draw-for-draw stable.
            if (params.applyFalloff)
            {
                const float normDist = std::clamp(r / radiusSafe, 0.0f, 1.0f);
                if (unitDist(rng) > terrain::applyFalloff(normDist, params.falloff))
                    continue;
            }

            float candX = brushCenter.x + r * std::cos(angle);
            float candZ = brushCenter.z + r * std::sin(angle);
            candX += jitterDist(rng) * params.positionJitter * params.spacing;
            candZ += jitterDist(rng) * params.positionJitter * params.spacing;

            const std::optional<float> h = env.heightAt(candX, candZ);
            if (!h.has_value()) continue;
            const glm::vec3 pos(candX, *h, candZ);

            // Spacing reject.
            if (env.spacingReject && env.spacingReject(pos)) continue;

            // Weighted type pick.
            const uint16_t typeIndex = static_cast<uint16_t>(enabledIndices[paletteDist(rng)]);
            const ScatterTypeRule& rule = rules[typeIndex];

            // Surface normal only when the picked rule needs it (slope band or align-to-normal).
            const bool ruleNeedsNormal =
                rule.alignToNormal || rule.minSlopeDeg > 0.0f || rule.maxSlopeDeg < 90.0f;
            glm::vec3 normal = fixedNormal;
            if (params.perCandidateNormal && ruleNeedsNormal && env.normalAt)
                normal = env.normalAt(candX, candZ);

            // Slope gate (wide-open rule 0..90 passes any normal).
            const float slopeDeg = glm::degrees(std::acos(glm::clamp(normal.y, -1.0f, 1.0f)));
            if (slopeDeg < rule.minSlopeDeg || slopeDeg > rule.maxSlopeDeg) continue;

            // Altitude gate.
            if (pos.y < rule.altitudeRange.x || pos.y > rule.altitudeRange.y) continue;

            // Accepted — build the candidate (transform draws mirror vegetation buildInstance).
            ScatterCandidate cand;
            cand.position = pos;
            cand.typeIndex = typeIndex;

            const float rotDeg = rule.rotationYRangeDeg.x +
                unitDist(rng) * (rule.rotationYRangeDeg.y - rule.rotationYRangeDeg.x);
            cand.rotationY = glm::radians(rotDeg);

            const float s  = rule.scaleRange.x  + unitDist(rng) * (rule.scaleRange.y  - rule.scaleRange.x);
            const float hm = rule.heightRange.x + unitDist(rng) * (rule.heightRange.y - rule.heightRange.x);
            cand.scale = glm::vec3(s, s * hm, s);

            // Normal + optional random tilt (only meaningful under align-to-normal, matching
            // composeFoliageModelMatrix, which ignores the normal when alignToNormal is off).
            glm::vec3 finalNormal(0.0f, 1.0f, 0.0f);
            if (rule.alignToNormal)
            {
                finalNormal = normal;
                if (rule.randomTiltDeg > 0.0f)
                {
                    const float tiltAngle = glm::radians(unitDist(rng) * rule.randomTiltDeg);
                    const float tiltAzim  = angleDist(rng);
                    const glm::vec3 axis(std::cos(tiltAzim), 0.0f, std::sin(tiltAzim));
                    finalNormal = glm::normalize(
                        glm::vec3(glm::rotate(glm::mat4(1.0f), tiltAngle, axis) * glm::vec4(finalNormal, 0.0f)));
                    cand.tilted = true;
                }
            }
            cand.normal = finalNormal;

            cand.windPhase = unitDist(rng);
            cand.seed = static_cast<uint32_t>(rng());

            if (env.accept) env.accept(pos, typeIndex);
            out.push_back(cand);
            ++placed;
        }

        return placed;
    }
}
